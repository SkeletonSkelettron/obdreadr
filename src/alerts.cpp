#include "alerts.h"

#include <QCoreApplication>
#include <QLocale>

namespace alerts {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Alerts", text);
}

QString num(double v, int decimals)
{
    return QLocale::c().toString(v, 'f', decimals);
}

constexpr int kCoolantPid = 0x05;
constexpr int kRpmPid = 0x0C;
constexpr int kFuelLevelPid = 0x2F;
constexpr int kModuleVoltagePid = 0x42;
constexpr int kAdapterVoltage = -1;

} // namespace

void Monitor::reset()
{
    const double limit = m_rpmLimit;
    *this = Monitor();
    m_rpmLimit = limit;
}

bool Monitor::fire(Trigger &trigger, double t, double cooldown)
{
    if (trigger.active || t - trigger.firedAt < cooldown)
        return false;
    trigger.active = true;
    trigger.firedAt = t;
    return true;
}

QList<Alert> Monitor::addReading(int pid, double t, double value)
{
    QList<Alert> out;
    switch (pid) {
    case kRpmPid:
        m_rpm = value;
        m_rpmAt = t;
        if (m_rpmLimit > 0) {
            if (value >= m_rpmLimit) {
                if (fire(m_rpmTrigger, t, kRpmCooldown))
                    out << Alert{QStringLiteral("rpm"), tr("Engine speed limit"),
                                 tr("%1 rpm (limit %2)").arg(num(value, 0), num(m_rpmLimit, 0))};
            } else if (value < m_rpmLimit - 200) {
                m_rpmTrigger.active = false;
            }
        }
        break;
    case kCoolantPid:
        if (value >= kCoolantHot) {
            if (fire(m_coolant, t, kCooldown))
                out << Alert{QStringLiteral("coolant"), tr("Engine overheating"),
                             tr("Coolant at %1 °C. Ease off; if it keeps rising, stop and let the engine cool down.")
                                 .arg(num(value, 0))};
        } else if (value < kCoolantClear) {
            m_coolant.active = false;
        }
        break;
    case kFuelLevelPid:
        if (value < kFuelLow && !m_fuelFired) {
            m_fuelFired = true;
            out << Alert{QStringLiteral("fuel"), tr("Low fuel"), tr("%1 % left in the tank.").arg(num(value, 0))};
        }
        break;
    case kModuleVoltagePid:
        m_moduleVoltage = true;
        voltage(out, t, value);
        break;
    case kAdapterVoltage:
        if (!m_moduleVoltage)
            voltage(out, t, value);
        break;
    default:
        break;
    }
    return out;
}

void Monitor::voltage(QList<Alert> &out, double t, double volts)
{
    const bool running = t - m_rpmAt <= 5 && m_rpm > 500;

    // Not charging: low voltage with the engine running, for a while.
    if (running && volts < kChargingLow) {
        if (m_charging.since < 0)
            m_charging.since = t;
        if (t - m_charging.since >= kChargingSeconds && fire(m_charging, t, kCooldown))
            out << Alert{QStringLiteral("charging"), tr("Battery not charging"),
                         tr("%1 V with the engine running: the alternator or its belt may have failed. The car now runs on the battery.")
                             .arg(num(volts, 1))};
    } else {
        m_charging.since = -1;
        if (volts > kChargingClear)
            m_charging.active = false;
    }

    // Overcharging, whatever the engine does.
    if (volts > kOverchargeHigh) {
        if (m_overcharging.since < 0)
            m_overcharging.since = t;
        if (t - m_overcharging.since >= kOverchargeSeconds && fire(m_overcharging, t, kCooldown))
            out << Alert{QStringLiteral("overcharging"), tr("Charging voltage too high"),
                         tr("%1 V: the voltage regulator may be faulty, which harms the battery and electronics.")
                             .arg(num(volts, 1))};
    } else {
        m_overcharging.since = -1;
        if (volts < kOverchargeClear)
            m_overcharging.active = false;
    }
}

QList<Alert> Monitor::setCheckEngine(double t, bool milOn, int codeCount)
{
    Q_UNUSED(t)
    QList<Alert> out;
    if (m_milKnown) {
        if (milOn && !m_milOn)
            out << Alert{QStringLiteral("mil"), tr("Check engine light on"),
                         tr("The engine computer found a fault. Open Diagnostics to read the code.")};
        else if (codeCount > m_codeCount)
            out << Alert{QStringLiteral("mil"), tr("New fault code"),
                         tr("The car stored another fault code. Open Diagnostics to read it.")};
    }
    m_milKnown = true;
    m_milOn = milOn;
    m_codeCount = codeCount;
    return out;
}

} // namespace alerts
