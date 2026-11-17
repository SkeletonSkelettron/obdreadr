#include "obd/monitors.h"

#include "obd/elmparser.h"
#include "obd/pids.h"

#include <QHash>

namespace obd {

namespace {

struct Scale {
    double factor;
    double offset;
    const char *unit;
    int decimals;
};

// Units and scaling of mode 06 values (SAE J1979, "unit and scaling IDs").
// Unsigned IDs are 0x01-0x7F; most signed IDs are the unsigned ones + 0x80.
const QHash<int, Scale> &unsignedScales()
{
    static const QHash<int, Scale> scales = {
        {0x01, {1, 0, "", 0}},           {0x02, {0.1, 0, "", 1}},
        {0x03, {0.01, 0, "", 2}},        {0x04, {0.001, 0, "", 3}},
        {0x05, {0.0000305, 0, "", 5}},   {0x06, {0.000305, 0, "", 4}},
        {0x07, {0.25, 0, "rpm", 2}},     {0x08, {0.01, 0, "km/h", 2}},
        {0x09, {1, 0, "km/h", 0}},       {0x0A, {0.122, 0, "mV", 1}},
        {0x0B, {0.001, 0, "V", 3}},      {0x0C, {0.01, 0, "V", 2}},
        {0x0D, {0.00390625, 0, "mA", 3}}, {0x0E, {0.001, 0, "A", 3}},
        {0x0F, {0.01, 0, "A", 2}},       {0x10, {1, 0, "ms", 0}},
        {0x11, {100, 0, "ms", 0}},       {0x12, {1, 0, "s", 0}},
        {0x13, {1, 0, "mΩ", 0}},         {0x14, {1, 0, "Ω", 0}},
        {0x15, {1, 0, "kΩ", 0}},         {0x16, {0.1, -40, "°C", 1}},
        {0x17, {0.01, 0, "kPa", 2}},     {0x18, {0.0117, 0, "kPa", 2}},
        {0x19, {0.079, 0, "kPa", 1}},    {0x1A, {1, 0, "kPa", 0}},
        {0x1B, {10, 0, "kPa", 0}},       {0x1C, {0.01, 0, "°", 2}},
        {0x1D, {0.5, 0, "°", 1}},        {0x1E, {0.0000305, 0, "λ", 4}},
        {0x1F, {0.05, 0, "A/F", 2}},     {0x20, {0.0039062, 0, "", 3}},
        {0x21, {1, 0, "mHz", 0}},        {0x22, {1, 0, "Hz", 0}},
        {0x23, {1, 0, "kHz", 0}},        {0x24, {1, 0, "counts", 0}},
        {0x25, {1, 0, "km", 0}},         {0x26, {0.1, 0, "mV/ms", 1}},
        {0x27, {0.01, 0, "g/s", 2}},     {0x28, {1, 0, "g/s", 0}},
        {0x29, {0.25, 0, "Pa/s", 2}},    {0x2A, {0.001, 0, "kg/h", 3}},
        {0x2B, {1, 0, "switches", 0}},   {0x2C, {0.01, 0, "g/cyl", 2}},
        {0x2D, {0.01, 0, "mg/stroke", 2}}, {0x2E, {1, 0, "", 0}},
        {0x2F, {0.01, 0, "%", 2}},       {0x30, {0.001526, 0, "%", 3}},
        {0x31, {0.001, 0, "L", 3}},      {0x34, {1, 0, "min", 0}},
        {0x35, {10, 0, "ms", 0}},        {0x36, {0.01, 0, "g", 2}},
        {0x37, {0.1, 0, "g", 1}},        {0x38, {1, 0, "g", 0}},
        {0x3D, {0.01, 0, "mA", 2}},
    };
    return scales;
}

const QHash<int, Scale> &signedScales()
{
    static const QHash<int, Scale> scales = {
        {0x96, {0.1, 0, "°C", 1}},       {0xAD, {0.01, 0, "mg/stroke", 2}},
        {0xAE, {0.1, 0, "mg/stroke", 1}}, {0xAF, {0.01, 0, "%", 2}},
        {0xB0, {0.003052, 0, "%", 3}},   {0xB1, {2, 0, "mV/s", 0}},
        {0xFC, {0.01, 0, "kPa", 2}},     {0xFD, {0.001, 0, "kPa", 3}},
        {0xFE, {0.25, 0, "Pa", 2}},
    };
    return scales;
}

bool isSigned(quint8 uasid)
{
    return uasid & 0x80;
}

Scale scaleFor(quint8 uasid)
{
    if (isSigned(uasid)) {
        if (const auto it = signedScales().find(uasid); it != signedScales().end())
            return *it;
        if (const auto it = unsignedScales().find(uasid & 0x7F); it != unsignedScales().end() && (uasid & 0x7F) != 0x16)
            return *it;
    } else if (const auto it = unsignedScales().find(uasid); it != unsignedScales().end()) {
        return *it;
    }
    return {1, 0, "", 0};
}

QString bankSensor(int index, int sensorsPerBank)
{
    return QStringLiteral("bank %1 sensor %2").arg(index / sensorsPerBank + 1).arg(index % sensorsPerBank + 1);
}

} // namespace

QList<Monitor> readiness(const QByteArray &pid01)
{
    if (pid01.size() < 4)
        return {};
    const auto b = quint8(pid01.at(1));
    const auto c = quint8(pid01.at(2));
    const auto d = quint8(pid01.at(3));
    const bool diesel = b & 0x08;

    // Byte B: continuous monitors; bits 0-2 supported, bits 4-6 not complete.
    QList<Monitor> monitors = {
        {QStringLiteral("Misfire"), bool(b & 0x01), !(b & 0x10)},
        {QStringLiteral("Fuel system"), bool(b & 0x02), !(b & 0x20)},
        {QStringLiteral("Components"), bool(b & 0x04), !(b & 0x40)},
    };
    // Bytes C and D: the other monitors, supported and not complete.
    static const char *kSpark[] = {"Catalyst", "Heated catalyst", "Evaporative system", "Secondary air",
                                   "A/C refrigerant", "O2 sensor", "O2 sensor heater", "EGR / VVT"};
    static const char *kDiesel[] = {"NMHC catalyst", "NOx / SCR aftertreatment", nullptr, "Boost pressure",
                                    nullptr, "Exhaust gas sensor", "Particulate filter", "EGR / VVT"};
    for (int bit = 0; bit < 8; ++bit) {
        const char *name = diesel ? kDiesel[bit] : kSpark[bit];
        if (name)
            monitors << Monitor{QString::fromLatin1(name), bool(c & (1 << bit)), !(d & (1 << bit))};
    }
    return monitors;
}

QList<TestResult> parseTests(const QStringList &lines)
{
    QList<TestResult> results;
    for (const QByteArray &message : elm::messages(lines)) {
        if (message.isEmpty() || quint8(message.at(0)) != 0x46)
            continue;
        for (qsizetype i = 1; i + 9 <= message.size(); i += 9) {
            const auto byte = [&](qsizetype k) { return quint8(message.at(i + k)); };
            const quint8 uasid = byte(2);
            const auto word = [&](qsizetype k) -> qint32 {
                const quint16 raw = quint16(byte(k) << 8 | byte(k + 1));
                return isSigned(uasid) ? qint32(qint16(raw)) : qint32(raw);
            };
            const qint32 value = word(3);
            const qint32 min = word(5);
            const qint32 max = word(7);
            const Scale scale = scaleFor(uasid);

            TestResult r;
            r.mid = byte(0);
            r.tid = byte(1);
            r.uasid = uasid;
            r.value = value * scale.factor + scale.offset;
            r.min = min * scale.factor + scale.offset;
            r.max = max * scale.factor + scale.offset;
            r.unit = QString::fromUtf8(scale.unit);
            r.decimals = scale.decimals;
            r.hasLimits = min != 0 || max != 0;
            r.passed = !r.hasLimits || (value >= min && value <= max);
            results << r;
        }
    }
    return results;
}

QString monitorName(quint8 mid)
{
    if (mid >= 0x01 && mid <= 0x10)
        return QStringLiteral("O2 sensor, ") + bankSensor(mid - 0x01, 4);
    if (mid >= 0x21 && mid <= 0x24)
        return QStringLiteral("Catalyst, bank %1").arg(mid - 0x20);
    if (mid >= 0x31 && mid <= 0x34)
        return QStringLiteral("EGR / VVT, bank %1").arg(mid - 0x30);
    if (mid >= 0x35 && mid <= 0x38)
        return QStringLiteral("VVT, bank %1").arg(mid - 0x34);
    switch (mid) {
    case 0x39: return QStringLiteral("Evaporative system, cap off or 0.150\" leak");
    case 0x3A: return QStringLiteral("Evaporative system, 0.090\" leak");
    case 0x3B: return QStringLiteral("Evaporative system, 0.040\" leak");
    case 0x3C: return QStringLiteral("Evaporative system, 0.020\" leak");
    case 0x3D: return QStringLiteral("Purge flow");
    case 0xA1: return QStringLiteral("Misfire, all cylinders");
    default: break;
    }
    if (mid >= 0x41 && mid <= 0x50)
        return QStringLiteral("O2 sensor heater, ") + bankSensor(mid - 0x41, 4);
    if (mid >= 0x61 && mid <= 0x64)
        return QStringLiteral("Heated catalyst, bank %1").arg(mid - 0x60);
    if (mid >= 0x71 && mid <= 0x74)
        return QStringLiteral("Secondary air %1").arg(mid - 0x70);
    if (mid >= 0x81 && mid <= 0x84)
        return QStringLiteral("Fuel system, bank %1").arg(mid - 0x80);
    if (mid >= 0x85 && mid <= 0x88)
        return QStringLiteral("Boost pressure control, bank %1").arg(mid - 0x84);
    if (mid >= 0x90 && mid <= 0x91)
        return QStringLiteral("NOx adsorber, bank %1").arg(mid - 0x8F);
    if (mid >= 0x98 && mid <= 0x99)
        return QStringLiteral("NOx catalyst, bank %1").arg(mid - 0x97);
    if (mid >= 0xA2 && mid <= 0xAD)
        return QStringLiteral("Misfire, cylinder %1").arg(mid - 0xA1);
    if (mid >= 0xB0 && mid <= 0xB1)
        return QStringLiteral("Particulate filter, bank %1").arg(mid - 0xAF);
    return QStringLiteral("Monitor %1").arg(hex2(mid));
}

QString testName(quint8 mid, quint8 tid)
{
    if (mid >= 0xA1 && mid <= 0xAD) {
        if (tid == 0x0B)
            return QStringLiteral("Misfires, average of the last 10 drives");
        if (tid == 0x0C)
            return QStringLiteral("Misfires, last or current drive");
    }
    if (mid >= 0x01 && mid <= 0x10) {
        static const char *kO2Tests[] = {
            nullptr,
            "Rich to lean threshold voltage",
            "Lean to rich threshold voltage",
            "Low voltage for switch time",
            "High voltage for switch time",
            "Rich to lean switch time",
            "Lean to rich switch time",
            "Minimum voltage",
            "Maximum voltage",
            "Time between transitions",
            "Sensor period",
        };
        if (tid >= 0x01 && tid <= 0x0A)
            return QString::fromLatin1(kO2Tests[tid]);
    }
    return tid >= 0x80 ? QStringLiteral("Test %1 (maker-defined)").arg(hex2(tid))
                       : QStringLiteral("Test %1").arg(hex2(tid));
}

} // namespace obd
