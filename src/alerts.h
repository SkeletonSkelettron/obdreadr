#pragma once

#include <QList>
#include <QString>

// Warnings while driving: an overheating engine, an alternator that stopped
// charging or overcharges, the check engine light coming on, an engine speed
// limit and low fuel. Fed every reading; each alert fires once, clears when
// the value is back well inside its limit (hysteresis) and may fire again
// only after a cooldown.
namespace alerts {

struct Alert {
    QString key;   // "coolant", "charging", "overcharging", "mil", "rpm", "fuel"
    QString title;
    QString text;
};

class Monitor
{
public:
    // Engine speed that raises an alert; 0 turns it off.
    void setRpmLimit(double rpm) { m_rpmLimit = rpm; }
    double rpmLimit() const { return m_rpmLimit; }

    // Forgets everything, for a new connection.
    void reset();

    // One reading of a mode 01 PID at session time t (seconds); pid -1 is the
    // adapter's battery reading. Returns the alerts it raises.
    QList<Alert> addReading(int pid, double t, double value);

    // The check engine light and the stored code count from PID 01. The
    // first call only sets the baseline; later ones alert when the light
    // turns on or the count goes up.
    QList<Alert> setCheckEngine(double t, bool milOn, int codeCount);

    static constexpr double kCoolantHot = 108;
    static constexpr double kCoolantClear = 103;
    static constexpr double kChargingLow = 12.8;
    static constexpr double kChargingClear = 13.2;
    static constexpr double kChargingSeconds = 20;
    static constexpr double kOverchargeHigh = 15.3;
    static constexpr double kOverchargeClear = 15.0;
    static constexpr double kOverchargeSeconds = 10;
    static constexpr double kFuelLow = 10;
    static constexpr double kCooldown = 300;
    static constexpr double kRpmCooldown = 30;

private:
    struct Trigger {
        bool active = false;  // fired and not yet cleared
        double since = -1;    // when the condition started, for durations
        double firedAt = -1e9;
    };

    // Fires `trigger` when it is not active and its cooldown has passed.
    static bool fire(Trigger &trigger, double t, double cooldown);
    void voltage(QList<Alert> &out, double t, double volts);

    double m_rpmLimit = 0;
    double m_rpm = 0;
    double m_rpmAt = -1e9;
    bool m_moduleVoltage = false; // PID 42 seen: ignore the adapter's reading
    Trigger m_coolant;
    Trigger m_charging;
    Trigger m_overcharging;
    Trigger m_rpmTrigger;
    bool m_fuelFired = false;
    bool m_milKnown = false;
    bool m_milOn = false;
    int m_codeCount = 0;
};

} // namespace alerts
