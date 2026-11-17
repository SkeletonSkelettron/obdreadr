#include "obd/pids.h"

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
    {0x04, "Engine load", "%", 0, 100, 1, 1, true, percent},
    {0x05, "Coolant temperature", "°C", -40, 215, 0, 1, true, temperature},
    {0x06, "Short term fuel trim, bank 1", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x07, "Long term fuel trim, bank 1", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x08, "Short term fuel trim, bank 2", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x09, "Long term fuel trim, bank 2", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x0A, "Fuel pressure", "kPa", 0, 765, 0, 1, false,
     [](const quint8 *d) { return 3.0 * d[0]; }},
    {0x0B, "Intake manifold pressure", "kPa", 0, 255, 0, 1, true, a},
    {0x0C, "Engine speed", "rpm", 0, 8000, 0, 2, true,
     [](const quint8 *d) { return ab(d) / 4.0; }},
    {0x0D, "Vehicle speed", "km/h", 0, 255, 0, 1, true, a},
    {0x0E, "Timing advance", "°", -64, 63.5, 1, 1, false,
     [](const quint8 *d) { return d[0] / 2.0 - 64.0; }},
    {0x0F, "Intake air temperature", "°C", -40, 215, 0, 1, true, temperature},
    {0x10, "Mass air flow", "g/s", 0, 655.35, 2, 2, true,
     [](const quint8 *d) { return ab(d) / 100.0; }},
    {0x11, "Throttle position", "%", 0, 100, 1, 1, true, percent},
    {0x14, "O2 sensor 1 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x15, "O2 sensor 2 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x16, "O2 sensor 3 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x17, "O2 sensor 4 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x18, "O2 sensor 5 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x19, "O2 sensor 6 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x1A, "O2 sensor 7 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x1B, "O2 sensor 8 voltage", "V", 0, 1.275, 3, 1, false, o2Voltage},
    {0x1F, "Run time since engine start", "s", 0, 65535, 0, 2, false, ab},
    {0x21, "Distance with MIL on", "km", 0, 65535, 0, 2, false, ab},
    {0x22, "Fuel rail pressure (vacuum)", "kPa", 0, 5177, 1, 2, false,
     [](const quint8 *d) { return ab(d) * 0.079; }},
    {0x23, "Fuel rail gauge pressure", "kPa", 0, 655350, 0, 2, false,
     [](const quint8 *d) { return ab(d) * 10.0; }},
    {0x24, "O2 sensor 1 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x25, "O2 sensor 2 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x26, "O2 sensor 3 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x27, "O2 sensor 4 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x28, "O2 sensor 5 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x29, "O2 sensor 6 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x2A, "O2 sensor 7 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x2B, "O2 sensor 8 lambda", "λ", 0, 2, 3, 2, false, lambda},
    {0x2C, "Commanded EGR", "%", 0, 100, 1, 1, false, percent},
    {0x2D, "EGR error", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x2E, "Commanded evaporative purge", "%", 0, 100, 1, 1, false, percent},
    {0x2F, "Fuel tank level", "%", 0, 100, 1, 1, true, percent},
    {0x30, "Warm-ups since codes cleared", "", 0, 255, 0, 1, false, a},
    {0x31, "Distance since codes cleared", "km", 0, 65535, 0, 2, false, ab},
    {0x32, "Evap system vapor pressure", "Pa", -8192, 8192, 1, 2, false,
     [](const quint8 *d) { return signedAb(d) / 4.0; }},
    {0x33, "Barometric pressure", "kPa", 0, 255, 0, 1, false, a},
    {0x34, "O2 sensor 1 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x35, "O2 sensor 2 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x36, "O2 sensor 3 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x37, "O2 sensor 4 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x38, "O2 sensor 5 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x39, "O2 sensor 6 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x3A, "O2 sensor 7 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x3B, "O2 sensor 8 current", "mA", -128, 128, 2, 4, false, o2Current},
    {0x3C, "Catalyst temperature, bank 1 sensor 1", "°C", -40, 1000, 0, 2, false, catalyst},
    {0x3D, "Catalyst temperature, bank 2 sensor 1", "°C", -40, 1000, 0, 2, false, catalyst},
    {0x3E, "Catalyst temperature, bank 1 sensor 2", "°C", -40, 1000, 0, 2, false, catalyst},
    {0x3F, "Catalyst temperature, bank 2 sensor 2", "°C", -40, 1000, 0, 2, false, catalyst},
    {0x42, "Control module voltage", "V", 0, 16, 2, 2, true,
     [](const quint8 *d) { return ab(d) / 1000.0; }},
    {0x43, "Absolute load", "%", 0, 100, 1, 2, false,
     [](const quint8 *d) { return ab(d) * 100.0 / 255.0; }},
    {0x44, "Commanded air-fuel ratio", "λ", 0, 2, 3, 2, false, lambda},
    {0x45, "Relative throttle position", "%", 0, 100, 1, 1, false, percent},
    {0x46, "Ambient air temperature", "°C", -40, 215, 0, 1, false, temperature},
    {0x47, "Absolute throttle position B", "%", 0, 100, 1, 1, false, percent},
    {0x48, "Absolute throttle position C", "%", 0, 100, 1, 1, false, percent},
    {0x49, "Accelerator pedal position D", "%", 0, 100, 1, 1, false, percent},
    {0x4A, "Accelerator pedal position E", "%", 0, 100, 1, 1, false, percent},
    {0x4B, "Accelerator pedal position F", "%", 0, 100, 1, 1, false, percent},
    {0x4C, "Commanded throttle actuator", "%", 0, 100, 1, 1, false, percent},
    {0x4D, "Time run with MIL on", "min", 0, 65535, 0, 2, false, ab},
    {0x4E, "Time since codes cleared", "min", 0, 65535, 0, 2, false, ab},
    {0x52, "Ethanol fuel", "%", 0, 100, 1, 1, false, percent},
    {0x53, "Absolute evap system vapor pressure", "kPa", 0, 327.675, 2, 2, false,
     [](const quint8 *d) { return ab(d) / 200.0; }},
    {0x54, "Evap system vapor pressure", "Pa", -32768, 32767, 0, 2, false, signedAb},
    {0x55, "Short term secondary O2 trim, bank 1", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x56, "Long term secondary O2 trim, bank 1", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x57, "Short term secondary O2 trim, bank 2", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x58, "Long term secondary O2 trim, bank 2", "%", -100, 99.2, 1, 1, false, fuelTrim},
    {0x59, "Fuel rail absolute pressure", "kPa", 0, 655350, 0, 2, false,
     [](const quint8 *d) { return ab(d) * 10.0; }},
    {0x5A, "Relative accelerator pedal position", "%", 0, 100, 1, 1, false, percent},
    {0x5B, "Hybrid battery remaining life", "%", 0, 100, 1, 1, false, percent},
    {0x5C, "Engine oil temperature", "°C", -40, 210, 0, 1, true, temperature},
    {0x5D, "Fuel injection timing", "°", -210, 302, 1, 2, false,
     [](const quint8 *d) { return ab(d) / 128.0 - 210.0; }},
    {0x5E, "Engine fuel rate", "L/h", 0, 3276.75, 2, 2, true,
     [](const quint8 *d) { return ab(d) / 20.0; }},
    {0x61, "Driver's demand engine torque", "%", -125, 130, 0, 1, false, torque},
    {0x62, "Actual engine torque", "%", -125, 130, 0, 1, false, torque},
    {0x63, "Engine reference torque", "Nm", 0, 65535, 0, 2, false, ab},
    {0xA6, "Odometer", "km", 0, 429496729.5, 1, 4, false,
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

std::optional<double> decode(quint8 pid, const QByteArray &data)
{
    const PidInfo *info = findPid(pid);
    if (!info || data.size() < info->bytes)
        return std::nullopt;
    return info->decode(reinterpret_cast<const quint8 *>(data.constData()));
}

bool isSupportMaskPid(quint8 pid)
{
    return pid % 0x20 == 0;
}

QList<quint8> pidsFromMask(quint32 mask, quint8 basePid)
{
    QList<quint8> pids;
    for (int bit = 0; bit < 32; ++bit) {
        if (mask & (0x80000000u >> bit))
            pids << quint8(basePid + 1 + bit);
    }
    return pids;
}

QString hex2(quint8 value)
{
    return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace obd
