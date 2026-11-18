#include "alerts.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

namespace alerts {

namespace {

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
        if (value > 500 && m_runningSince < 0)
            m_runningSince = t;
        else if (value < 300)
            m_runningSince = -1;
        if (m_rpmLimit > 0) {
            if (value >= m_rpmLimit) {
                if (fire(m_rpmTrigger, t, kRpmCooldown))
                    out << Alert{QStringLiteral("rpm"), QCoreApplication::translate("Alerts", "Engine speed limit"),
                                 QCoreApplication::translate("Alerts", "%1 rpm (limit %2)").arg(num(value, 0), num(m_rpmLimit, 0))};
            } else if (value < m_rpmLimit - 200) {
                m_rpmTrigger.active = false;
            }
        }
        break;
    case kCoolantPid:
        if (value >= kCoolantHot) {
            if (fire(m_coolant, t, kCooldown))
                out << Alert{QStringLiteral("coolant"), QCoreApplication::translate("Alerts", "Engine overheating"),
                             QCoreApplication::translate("Alerts", "Coolant at %1 °C. Ease off; if it keeps rising, stop and let the engine cool down.")
                                 .arg(num(value, 0))};
        } else if (value < kCoolantClear) {
            m_coolant.active = false;
        }
        break;
    case kFuelLevelPid:
        if (value < kFuelLow && !m_fuelFired) {
            m_fuelFired = true;
            out << Alert{QStringLiteral("fuel"), QCoreApplication::translate("Alerts", "Low fuel"), QCoreApplication::translate("Alerts", "%1 % left in the tank.").arg(num(value, 0))};
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

    if (running && volts >= kChargedVolts) {
        m_lastCharge = t;
        m_charging.active = false;
    }

    // Not charging: no charging voltage at all for 15 minutes of running.
    if (running && volts < kNotChargingVolts && m_runningSince >= 0) {
        const double since = std::max(m_lastCharge, m_runningSince);
        if (t - since >= kNoChargeSeconds && fire(m_charging, t, kCooldown))
            out << Alert{QStringLiteral("charging"), QCoreApplication::translate("Alerts", "Battery not charging"),
                         QCoreApplication::translate("Alerts", "No charging for %1 minutes with the engine running (%2 V): the alternator or its belt may have failed, and the car runs on the battery.")
                             .arg(num((t - since) / 60, 0), num(volts, 1))};
    }

    // Drained: whatever the charging strategy, this low means trouble.
    if (running && volts < kDrainedVolts) {
        if (m_drained.since < 0)
            m_drained.since = t;
        if (t - m_drained.since >= kDrainedSeconds && fire(m_drained, t, kCooldown))
            out << Alert{QStringLiteral("battery"), QCoreApplication::translate("Alerts", "Battery voltage low"),
                         QCoreApplication::translate("Alerts", "%1 V with the engine running: the battery is being drained. Check the alternator, its belt and the battery.")
                             .arg(num(volts, 1))};
    } else {
        m_drained.since = -1;
        if (volts > kDrainedClear)
            m_drained.active = false;
    }

    // Overcharging, whatever the engine does.
    if (volts > kOverchargeHigh) {
        if (m_overcharging.since < 0)
            m_overcharging.since = t;
        if (t - m_overcharging.since >= kOverchargeSeconds && fire(m_overcharging, t, kCooldown))
            out << Alert{QStringLiteral("overcharging"), QCoreApplication::translate("Alerts", "Charging voltage too high"),
                         QCoreApplication::translate("Alerts", "%1 V: the voltage regulator may be faulty, which harms the battery and electronics.")
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
            out << Alert{QStringLiteral("mil"), QCoreApplication::translate("Alerts", "Check engine light on"),
                         QCoreApplication::translate("Alerts", "The engine computer found a fault. Open Diagnostics to read the code.")};
        else if (codeCount > m_codeCount)
            out << Alert{QStringLiteral("mil"), QCoreApplication::translate("Alerts", "New fault code"),
                         QCoreApplication::translate("Alerts", "The car stored another fault code. Open Diagnostics to read it.")};
    }
    m_milKnown = true;
    m_milOn = milOn;
    m_codeCount = codeCount;
    return out;
}

} // namespace alerts
