#include "obd/pids.h"

#include <QCoreApplication>

#include <array>

namespace obd {

namespace {

double a(const quint8 *d) { return d[0]; }
double ab(const quint8 *d) { return 256.0 * d[0] + d[1]; }
double cd(const quint8 *d) { return 256.0 * d[2] + d[3]; }
double percent(const quint8 *d) { return d[0] * 100.0 / 255.0; }
double temperature(const quint8 *d) { return d[0] - 40.0; }
double fuelTrim(const quint8 *d) { return d[0] * 100.0 / 128.0 - 100.0; }
double torque(const quint8 *d) { return d[0] - 125.0; }
double signedAb(const quint8 *d) { return double(qint16(quint16(d[0] << 8 | d[1]))); }
double o2Voltage(const quint8 *d) { return d[0] / 200.0; }
double lambda(const quint8 *d) { return ab(d) * 2.0 / 65536.0; }
double o2Current(const quint8 *d) { return cd(d) / 256.0 - 128.0; }
double catalyst(const quint8 *d) { return ab(d) / 10.0 - 40.0; }

const PidInfo kPids[] = {
    {0x04, QT_TRANSLATE_NOOP("Pids", "Engine load"), "%", 0, 100, 1, 1, true, percent},
    {0x05, QT_TRANSLATE_NOOP("Pids", "Coolant temperature"), "°C", -40, 215, 0, 1, true, temperature},
    {0x06, QT_TRANSLATE_NOOP("Pids", "Short term fuel trim, bank 1"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x07, QT_TRANSLATE_NOOP("Pids", "Long term fuel trim, bank 1"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x08, QT_TRANSLATE_NOOP("Pids", "Short term fuel trim, bank 2"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x09, QT_TRANSLATE_NOOP("Pids", "Long term fuel trim, bank 2"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x0A, QT_TRANSLATE_NOOP("Pids", "Fuel pressure"), "kPa", 0, 765, 0, 1, false,
     [](const quint8 *d) { return 3.0 * d[0]; }},
    {0x0B, QT_TRANSLATE_NOOP("Pids", "Intake manifold pressure"), "kPa", 0, 255, 0, 1, true, a},
    {0x0C, QT_TRANSLATE_NOOP("Pids", "Engine speed"), "rpm", 0, 8000, 0, 2, true,
     [](const quint8 *d) { return ab(d) / 4.0; }},
    {0x0D, QT_TRANSLATE_NOOP("Pids", "Vehicle speed"), "km/h", 0, 255, 0, 1, true, a},
    {0x0E, QT_TRANSLATE_NOOP("Pids", "Timing advance"), "°", -64, 63.5, 1, 1, false,
     [](const quint8 *d) { return d[0] / 2.0 - 64.0; }},
    {0x0F, QT_TRANSLATE_NOOP("Pids", "Intake air temperature"), "°C", -40, 215, 0, 1, true, temperature},
    {0x10, QT_TRANSLATE_NOOP("Pids", "Mass air flow"), "g/s", 0, 655.35, 2, 2, true,
     [](const quint8 *d) { return ab(d) / 100.0; }},
    {0x11, QT_TRANSLATE_NOOP("Pids", "Throttle position"), "%", 0, 100, 1, 1, true, percent},
    {0x14, QT_TRANSLATE_NOOP("Pids", "O2 sensor 1 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x15, QT_TRANSLATE_NOOP("Pids", "O2 sensor 2 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x16, QT_TRANSLATE_NOOP("Pids", "O2 sensor 3 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x17, QT_TRANSLATE_NOOP("Pids", "O2 sensor 4 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x18, QT_TRANSLATE_NOOP("Pids", "O2 sensor 5 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x19, QT_TRANSLATE_NOOP("Pids", "O2 sensor 6 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x1A, QT_TRANSLATE_NOOP("Pids", "O2 sensor 7 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x1B, QT_TRANSLATE_NOOP("Pids", "O2 sensor 8 voltage"), "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x1F, QT_TRANSLATE_NOOP("Pids", "Run time since engine start"), "s", 0, 65535, 0, 2, false, ab},
    {0x21, QT_TRANSLATE_NOOP("Pids", "Distance with MIL on"), "km", 0, 65535, 0, 2, false, ab},
    {0x22, QT_TRANSLATE_NOOP("Pids", "Fuel rail pressure (vacuum)"), "kPa", 0, 5177, 1, 2, false,
     [](const quint8 *d) { return ab(d) * 0.079; }},
    {0x23, QT_TRANSLATE_NOOP("Pids", "Fuel rail gauge pressure"), "kPa", 0, 655350, 0, 2, false,
     [](const quint8 *d) { return ab(d) * 10.0; }},
    {0x24, QT_TRANSLATE_NOOP("Pids", "O2 sensor 1 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x25, QT_TRANSLATE_NOOP("Pids", "O2 sensor 2 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x26, QT_TRANSLATE_NOOP("Pids", "O2 sensor 3 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x27, QT_TRANSLATE_NOOP("Pids", "O2 sensor 4 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x28, QT_TRANSLATE_NOOP("Pids", "O2 sensor 5 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x29, QT_TRANSLATE_NOOP("Pids", "O2 sensor 6 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x2A, QT_TRANSLATE_NOOP("Pids", "O2 sensor 7 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x2B, QT_TRANSLATE_NOOP("Pids", "O2 sensor 8 lambda"), "λ", 0, 2, 3, 2, false, lambda},
    {0x2C, QT_TRANSLATE_NOOP("Pids", "Commanded EGR"), "%", 0, 100, 1, 1, false, percent},
    {0x2D, QT_TRANSLATE_NOOP("Pids", "EGR error"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x2E, QT_TRANSLATE_NOOP("Pids", "Commanded evaporative purge"), "%", 0, 100, 1, 1, false, percent},
    {0x2F, QT_TRANSLATE_NOOP("Pids", "Fuel tank level"), "%", 0, 100, 1, 1, true, percent},
    {0x30, QT_TRANSLATE_NOOP("Pids", "Warm-ups since codes cleared"), "", 0, 255, 0, 1, false, a},
    {0x31, QT_TRANSLATE_NOOP("Pids", "Distance since codes cleared"), "km", 0, 65535, 0, 2, false, ab},
    {0x32, QT_TRANSLATE_NOOP("Pids", "Evap system vapor pressure"), "Pa", -8192, 8192, 1, 2, false,
     [](const quint8 *d) { return signedAb(d) / 4.0; }},
    {0x33, QT_TRANSLATE_NOOP("Pids", "Barometric pressure"), "kPa", 0, 255, 0, 1, false, a},
    {0x34, QT_TRANSLATE_NOOP("Pids", "O2 sensor 1 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x35, QT_TRANSLATE_NOOP("Pids", "O2 sensor 2 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x36, QT_TRANSLATE_NOOP("Pids", "O2 sensor 3 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x37, QT_TRANSLATE_NOOP("Pids", "O2 sensor 4 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x38, QT_TRANSLATE_NOOP("Pids", "O2 sensor 5 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x39, QT_TRANSLATE_NOOP("Pids", "O2 sensor 6 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x3A, QT_TRANSLATE_NOOP("Pids", "O2 sensor 7 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x3B, QT_TRANSLATE_NOOP("Pids", "O2 sensor 8 current"), "mA", -128, 128, 2, 4, false, o2Current},
    {0x3C, QT_TRANSLATE_NOOP("Pids", "Catalyst temperature, bank 1 sensor 1"), "°C", -40, 1000, 0, 2, false, catalyst},
    {0x3D, QT_TRANSLATE_NOOP("Pids", "Catalyst temperature, bank 2 sensor 1"), "°C", -40, 1000, 0, 2, false, catalyst},
    {0x3E, QT_TRANSLATE_NOOP("Pids", "Catalyst temperature, bank 1 sensor 2"), "°C", -40, 1000, 0, 2, false, catalyst},
    {0x3F, QT_TRANSLATE_NOOP("Pids", "Catalyst temperature, bank 2 sensor 2"), "°C", -40, 1000, 0, 2, false, catalyst},
    {0x42, QT_TRANSLATE_NOOP("Pids", "Control module voltage"), "V", 0, 16, 2, 2, true,
     [](const quint8 *d) { return ab(d) / 1000.0; }},
    {0x43, QT_TRANSLATE_NOOP("Pids", "Absolute load"), "%", 0, 100, 1, 2, false,
     [](const quint8 *d) { return ab(d) * 100.0 / 255.0; }},
    {0x44, QT_TRANSLATE_NOOP("Pids", "Commanded air-fuel ratio"), "λ", 0, 2, 3, 2, false, lambda},
    {0x45, QT_TRANSLATE_NOOP("Pids", "Relative throttle position"), "%", 0, 100, 1, 1, false, percent},
    {0x46, QT_TRANSLATE_NOOP("Pids", "Ambient air temperature"), "°C", -40, 215, 0, 1, false, temperature},
    {0x47, QT_TRANSLATE_NOOP("Pids", "Absolute throttle position B"), "%", 0, 100, 1, 1, false, percent},
    {0x48, QT_TRANSLATE_NOOP("Pids", "Absolute throttle position C"), "%", 0, 100, 1, 1, false, percent},
    {0x49, QT_TRANSLATE_NOOP("Pids", "Accelerator pedal position D"), "%", 0, 100, 1, 1, false, percent},
    {0x4A, QT_TRANSLATE_NOOP("Pids", "Accelerator pedal position E"), "%", 0, 100, 1, 1, false, percent},
    {0x4B, QT_TRANSLATE_NOOP("Pids", "Accelerator pedal position F"), "%", 0, 100, 1, 1, false, percent},
    {0x4C, QT_TRANSLATE_NOOP("Pids", "Commanded throttle actuator"), "%", 0, 100, 1, 1, false, percent},
    {0x4D, QT_TRANSLATE_NOOP("Pids", "Time run with MIL on"), "min", 0, 65535, 0, 2, false, ab},
    {0x4E, QT_TRANSLATE_NOOP("Pids", "Time since codes cleared"), "min", 0, 65535, 0, 2, false, ab},
    {0x52, QT_TRANSLATE_NOOP("Pids", "Ethanol fuel"), "%", 0, 100, 1, 1, false, percent},
    {0x53, QT_TRANSLATE_NOOP("Pids", "Absolute evap system vapor pressure"), "kPa", 0, 327.675, 2, 2, false,
     [](const quint8 *d) { return ab(d) / 200.0; }},
    {0x54, QT_TRANSLATE_NOOP("Pids", "Evap system vapor pressure"), "Pa", -32768, 32767, 0, 2, false, signedAb},
    {0x55, QT_TRANSLATE_NOOP("Pids", "Short term secondary O2 trim, bank 1"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x56, QT_TRANSLATE_NOOP("Pids", "Long term secondary O2 trim, bank 1"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x57, QT_TRANSLATE_NOOP("Pids", "Short term secondary O2 trim, bank 2"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x58, QT_TRANSLATE_NOOP("Pids", "Long term secondary O2 trim, bank 2"), "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x59, QT_TRANSLATE_NOOP("Pids", "Fuel rail absolute pressure"), "kPa", 0, 655350, 0, 2, false,
     [](const quint8 *d) { return ab(d) * 10.0; }},
    {0x5A, QT_TRANSLATE_NOOP("Pids", "Relative accelerator pedal position"), "%", 0, 100, 1, 1, false, percent},
    {0x5B, QT_TRANSLATE_NOOP("Pids", "Hybrid battery remaining life"), "%", 0, 100, 1, 1, false, percent},
    {0x5C, QT_TRANSLATE_NOOP("Pids", "Engine oil temperature"), "°C", -40, 210, 0, 1, true, temperature},
    {0x5D, QT_TRANSLATE_NOOP("Pids", "Fuel injection timing"), "°", -210, 302, 1, 2, false,
     [](const quint8 *d) { return ab(d) / 128.0 - 210.0; }},
    {0x5E, QT_TRANSLATE_NOOP("Pids", "Engine fuel rate"), "L/h", 0, 3276.75, 2, 2, true,
     [](const quint8 *d) { return ab(d) / 20.0; }},
    {0x61, QT_TRANSLATE_NOOP("Pids", "Driver's demand engine torque"), "%", -125, 130, 0, 1, false, torque},
    {0x62, QT_TRANSLATE_NOOP("Pids", "Actual engine torque"), "%", -125, 130, 0, 1, false, torque},
    {0x63, QT_TRANSLATE_NOOP("Pids", "Engine reference torque"), "Nm", 0, 65535, 0, 2, false, ab},
    {0xA6, QT_TRANSLATE_NOOP("Pids", "Odometer"), "km", 0, 429496729.5, 1, 4, false,
     [](const quint8 *d) {
         return (double(d[0]) * 16777216.0 + d[1] * 65536.0 + d[2] * 256.0 + d[3]) / 10.0;
     }},
};

const std::array<const PidInfo *, 256> &lookup()
{
    static const std::array<const PidInfo *, 256> table = [] {
        std::array<const PidInfo *, 256> t{};
        for (const PidInfo &info : kPids)
            t[info.pid] = &info;
        return t;
    }();
    return table;
}

} // namespace

const PidInfo *findPid(quint8 pid)
{
    return lookup()[pid];
}

QString displayName(const PidInfo &info)
{
    return QCoreApplication::translate("Pids", info.name);
}

QString adapterVoltageName()
{
    return QStringLiteral("Battery voltage (adapter)");
}

int pidByName(const QString &name)
{
    if (name == adapterVoltageName())
        return -1;
    for (const PidInfo &info : kPids) {
        if (name == QString::fromUtf8(info.name))
            return info.pid;
    }
    return -2;
}

std::optional<double> decode(quint8 pid, const QByteArray &data)
{
    const PidInfo *info = findPid(pid);
    if (!info || data.size() < info->bytes)
        return std::nullopt;
    return info->decode(reinterpret_cast<const quint8 *>(data.constData()));
}

Pace pace(quint8 pid)
{
    switch (pid) {
    // Engine and driver: load, trims, pressures, speed, airflow, throttle,
    // oxygen sensors, pedal, fuel rate, torque.
    case 0x04: case 0x06: case 0x08: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
    case 0x0E: case 0x10: case 0x11: case 0x22: case 0x23: case 0x43: case 0x44:
    case 0x45: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C:
    case 0x59: case 0x5A: case 0x5E: case 0x61: case 0x62:
        return Pace::Fast;
    // Temperatures, levels, long term trims, counters and distances.
    case 0x05: case 0x07: case 0x09: case 0x0F: case 0x1F: case 0x21: case 0x2F:
    case 0x30: case 0x31: case 0x33: case 0x46: case 0x4D: case 0x4E: case 0x52:
    case 0x56: case 0x58: case 0x5B: case 0x5C: case 0x63: case 0xA6:
        return Pace::Slow;
    default:
        break;
    }
    if ((pid >= 0x14 && pid <= 0x1B) || (pid >= 0x24 && pid <= 0x2B) || (pid >= 0x34 && pid <= 0x3B))
        return Pace::Fast; // oxygen sensors
    return Pace::Medium;
}

bool isSupportMaskPid(quint8 pid)
{
    return pid % 0x20 == 0;
}

QList<quint8> pidsFromMask(quint32 mask, quint8 basePid)
{
    QList<quint8> pids;
    for (int bit = 0; bit < 32; ++bit) {
        const int pid = basePid + 1 + bit;
        if (pid <= 0xFF && (mask & (0x80000000u >> bit)))
            pids << quint8(pid);
    }
    return pids;
}

QString hex2(quint8 value)
{
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace obd
