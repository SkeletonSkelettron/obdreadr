#include "obd/dtc.h"

#include "obd/elmparser.h"

#include <QByteArray>
#include <QHash>
#include <QList>

namespace dtc {

namespace {

struct Message {
    QByteArray bytes;
    int length = -1; // from the ISO-TP length line, when there is one
};

// Splits answer lines into one message per ECU answer. A multi-frame CAN
// answer is a length line ("00A") followed by frames "0:", "1:", ...
QList<Message> messages(const QStringList &lines)
{
    QList<Message> result;
    int pendingLength = -1;
    for (const QString &line : lines) {
        const QString c = elm::compact(line);
        const int colon = c.indexOf(QLatin1Char(':'));
        if (colon > 0 && colon <= 2) {
            const QString hex = c.mid(colon + 1);
            if (!elm::isHex(hex))
                continue;
            if (c.left(colon) == QLatin1String("0") || result.isEmpty()) {
                result << Message{{}, pendingLength};
                pendingLength = -1;
            }
            result.last().bytes += QByteArray::fromHex(hex.toLatin1());
            continue;
        }
        if (c.size() == 3) {
            bool ok = false;
            const int length = c.toInt(&ok, 16);
            if (ok) {
                pendingLength = length;
                continue;
            }
        }
        if (elm::isHex(c))
            result << Message{QByteArray::fromHex(c.toLatin1()), -1};
    }
    for (Message &m : result) {
        if (m.length >= 0 && m.bytes.size() > m.length)
            m.bytes.truncate(m.length);
    }
    return result;
}

QHash<QString, QString> table()
{
    QHash<QString, QString> t = {
        {"P0010", "Camshaft position actuator circuit, bank 1"},
        {"P0011", "Camshaft timing over-advanced, bank 1"},
        {"P0012", "Camshaft timing over-retarded, bank 1"},
        {"P0016", "Crankshaft/camshaft position correlation, bank 1"},
        {"P0030", "O2 sensor heater control circuit, bank 1 sensor 1"},
        {"P0100", "Mass air flow circuit malfunction"},
        {"P0101", "Mass air flow circuit range/performance"},
        {"P0102", "Mass air flow circuit low"},
        {"P0103", "Mass air flow circuit high"},
        {"P0105", "Manifold pressure circuit malfunction"},
        {"P0106", "Manifold pressure range/performance"},
        {"P0107", "Manifold pressure circuit low"},
        {"P0108", "Manifold pressure circuit high"},
        {"P0110", "Intake air temperature circuit malfunction"},
        {"P0112", "Intake air temperature circuit low"},
        {"P0113", "Intake air temperature circuit high"},
        {"P0115", "Coolant temperature circuit malfunction"},
        {"P0116", "Coolant temperature range/performance"},
        {"P0117", "Coolant temperature circuit low"},
        {"P0118", "Coolant temperature circuit high"},
        {"P0120", "Throttle position sensor circuit malfunction"},
        {"P0121", "Throttle position sensor range/performance"},
        {"P0122", "Throttle position sensor circuit low"},
        {"P0123", "Throttle position sensor circuit high"},
        {"P0125", "Coolant temperature too low for closed loop"},
        {"P0128", "Thermostat: coolant below regulating temperature"},
        {"P0130", "O2 sensor circuit, bank 1 sensor 1"},
        {"P0131", "O2 sensor low voltage, bank 1 sensor 1"},
        {"P0132", "O2 sensor high voltage, bank 1 sensor 1"},
        {"P0133", "O2 sensor slow response, bank 1 sensor 1"},
        {"P0134", "O2 sensor no activity, bank 1 sensor 1"},
        {"P0135", "O2 sensor heater circuit, bank 1 sensor 1"},
        {"P0136", "O2 sensor circuit, bank 1 sensor 2"},
        {"P0137", "O2 sensor low voltage, bank 1 sensor 2"},
        {"P0138", "O2 sensor high voltage, bank 1 sensor 2"},
        {"P0139", "O2 sensor slow response, bank 1 sensor 2"},
        {"P0140", "O2 sensor no activity, bank 1 sensor 2"},
        {"P0141", "O2 sensor heater circuit, bank 1 sensor 2"},
        {"P0150", "O2 sensor circuit, bank 2 sensor 1"},
        {"P0155", "O2 sensor heater circuit, bank 2 sensor 1"},
        {"P0161", "O2 sensor heater circuit, bank 2 sensor 2"},
        {"P0170", "Fuel trim malfunction, bank 1"},
        {"P0171", "System too lean, bank 1"},
        {"P0172", "System too rich, bank 1"},
        {"P0173", "Fuel trim malfunction, bank 2"},
        {"P0174", "System too lean, bank 2"},
        {"P0175", "System too rich, bank 2"},
        {"P0190", "Fuel rail pressure sensor circuit"},
        {"P0200", "Injector circuit malfunction"},
        {"P0217", "Engine overheat condition"},
        {"P0230", "Fuel pump primary circuit"},
        {"P0234", "Turbocharger overboost"},
        {"P0299", "Turbocharger underboost"},
        {"P0300", "Random or multiple cylinder misfire detected"},
        {"P0325", "Knock sensor 1 circuit, bank 1"},
        {"P0327", "Knock sensor 1 circuit low, bank 1"},
        {"P0328", "Knock sensor 1 circuit high, bank 1"},
        {"P0335", "Crankshaft position sensor A circuit"},
        {"P0336", "Crankshaft position sensor A range/performance"},
        {"P0340", "Camshaft position sensor circuit, bank 1"},
        {"P0341", "Camshaft position sensor range/performance, bank 1"},
        {"P0400", "EGR flow malfunction"},
        {"P0401", "EGR flow insufficient"},
        {"P0402", "EGR flow excessive"},
        {"P0403", "EGR circuit malfunction"},
        {"P0420", "Catalyst efficiency below threshold, bank 1"},
        {"P0430", "Catalyst efficiency below threshold, bank 2"},
        {"P0440", "Evaporative emission system malfunction"},
        {"P0441", "Evaporative emission system incorrect purge flow"},
        {"P0442", "Evaporative emission system small leak"},
        {"P0443", "Evaporative emission purge valve circuit"},
        {"P0446", "Evaporative emission vent control circuit"},
        {"P0455", "Evaporative emission system large leak"},
        {"P0456", "Evaporative emission system very small leak"},
        {"P0457", "Evaporative emission leak (fuel cap loose or off)"},
        {"P0480", "Cooling fan 1 control circuit"},
        {"P0500", "Vehicle speed sensor malfunction"},
        {"P0505", "Idle control system malfunction"},
        {"P0506", "Idle speed lower than expected"},
        {"P0507", "Idle speed higher than expected"},
        {"P0562", "System voltage low"},
        {"P0563", "System voltage high"},
        {"P0571", "Brake switch A circuit"},
        {"P0600", "Serial communication link"},
        {"P0601", "Control module memory checksum error"},
        {"P0606", "Control module processor fault"},
        {"P0700", "Transmission control system malfunction"},
        {"P0705", "Transmission range sensor circuit"},
        {"P0715", "Input/turbine speed sensor circuit"},
        {"P0720", "Output speed sensor circuit"},
        {"P0730", "Incorrect gear ratio"},
        {"P0740", "Torque converter clutch circuit"},
        {"P0741", "Torque converter clutch stuck off"},
        {"P2096", "Post-catalyst fuel trim too lean, bank 1"},
        {"P2097", "Post-catalyst fuel trim too rich, bank 1"},
        {"P2135", "Throttle position sensor A/B voltage correlation"},
        {"P2187", "System too lean at idle, bank 1"},
        {"P2188", "System too rich at idle, bank 1"},
        {"U0001", "High speed CAN communication bus"},
        {"U0100", "Lost communication with engine control module"},
        {"U0101", "Lost communication with transmission control module"},
        {"U0121", "Lost communication with ABS control module"},
        {"U0140", "Lost communication with body control module"},
        {"U0155", "Lost communication with instrument cluster"},
    };
    for (int n = 1; n <= 12; ++n) {
        const QString cylinder = QString::number(n);
        t.insert(QStringLiteral("P03%1").arg(n, 2, 10, QLatin1Char('0')),
                 QStringLiteral("Cylinder %1 misfire detected").arg(cylinder));
        t.insert(QStringLiteral("P02%1").arg(n, 2, 10, QLatin1Char('0')),
                 QStringLiteral("Injector circuit, cylinder %1").arg(cylinder));
        t.insert(QStringLiteral("P03%1").arg(50 + n, 2, 10, QLatin1Char('0')),
                 QStringLiteral("Ignition coil %1 circuit").arg(cylinder));
    }
    return t;
}

QString category(const QString &code)
{
    if (code.size() != 5)
        return QString();
    const QChar system = code.at(0);
    const QChar group = code.at(1);
    if (group == QLatin1Char('1') || (system != QLatin1Char('P') && group == QLatin1Char('2')))
        return QStringLiteral("Manufacturer-specific code");
    if (system == QLatin1Char('B'))
        return QStringLiteral("Body");
    if (system == QLatin1Char('C'))
        return QStringLiteral("Chassis");
    if (system == QLatin1Char('U'))
        return QStringLiteral("Network communication");
    if (group == QLatin1Char('3'))
        return QStringLiteral("Powertrain (manufacturer-specific or extended)");
    switch (code.at(2).toLatin1()) {
    case '0':
    case '1':
    case '2': return QStringLiteral("Fuel and air metering");
    case '3': return QStringLiteral("Ignition system or misfire");
    case '4': return QStringLiteral("Auxiliary emission controls");
    case '5': return QStringLiteral("Vehicle speed, idle control or auxiliary inputs");
    case '6': return QStringLiteral("Computer or output circuits");
    case '7':
    case '8':
    case '9': return QStringLiteral("Transmission");
    case 'A':
    case 'B':
    case 'C': return QStringLiteral("Hybrid propulsion");
    default: return QStringLiteral("Powertrain");
    }
}

} // namespace

QString fromBytes(quint8 a, quint8 b)
{
    static const char kSystems[] = {'P', 'C', 'B', 'U'};
    return QStringLiteral("%1%2%3%4")
        .arg(QLatin1Char(kSystems[a >> 6]))
        .arg(int((a >> 4) & 0x3))
        .arg(int(a & 0xF), 1, 16)
        .arg(int(b), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QStringList parse(const QStringList &lines, quint8 service)
{
    const auto reply = char(service + 0x40);
    QStringList codes;
    for (const Message &m : messages(lines)) {
        if (m.bytes.isEmpty() || m.bytes.at(0) != reply)
            continue;
        QByteArray data = m.bytes.mid(1);
        // CAN answers start with the number of codes, which makes the data
        // length odd; the older protocols send pairs only.
        if (data.size() % 2 == 1) {
            const int count = quint8(data.at(0));
            data = data.mid(1, count * 2);
        }
        for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
            const auto a = quint8(data.at(i));
            const auto b = quint8(data.at(i + 1));
            if (a == 0 && b == 0)
                continue;
            const QString code = fromBytes(a, b);
            if (!codes.contains(code))
                codes << code;
        }
    }
    return codes;
}

QString describe(const QString &code)
{
    static const QHash<QString, QString> kTable = table();
    const QString known = kTable.value(code);
    return known.isEmpty() ? category(code) : known;
}

} // namespace dtc
