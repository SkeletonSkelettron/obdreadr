#pragma once

#include <vector>

// A battery test around starting the engine: the resting voltage with the
// ignition on, the lowest voltage while the starter cranks, and the charging
// voltage once the engine runs. Fed the adapter's voltage and the engine
// speed as fast as they come; pure, so the tests drive it directly.
namespace battery {

enum class Phase {
    Resting, // the first seconds: measuring the resting voltage, don't start yet
    Crank,   // start the engine now
    Running, // started: measuring the charging voltage
    Done,
};

enum class Verdict { None, Ok, Watch, Bad };

class Test
{
public:
    static constexpr double kRestSeconds = 3;     // resting voltage, before asking to crank
    static constexpr double kStartRpm = 500;      // the engine counts as started above this
    static constexpr double kChargeFrom = 5;      // charging voltage: seconds after the start...
    static constexpr double kChargeTo = 10;       // ...up to this, which ends the test
    static constexpr double kGiveUpSeconds = 60;  // no start within this ends it too

    explicit Test(double startTime);

    void addVolts(double t, double volts);
    void addRpm(double t, double rpm);
    // Moves the phase on by the clock, also without new readings.
    void tick(double t);

    Phase phase() const { return m_phase; }
    bool engineStarted() const { return m_startedAt >= 0; }
    // The engine was already running when the test began.
    bool engineWasRunning() const { return m_wasRunning; }

    // -1 when not measured.
    double rest() const;
    double cranking() const { return m_crankMin; }
    double charging() const;

    static Verdict restVerdict(double volts);
    static Verdict crankVerdict(double volts);
    static Verdict chargeVerdict(double volts);

private:
    double m_start;
    Phase m_phase = Phase::Resting;
    std::vector<double> m_rest;
    std::vector<double> m_charge;
    double m_crankMin = -1;
    double m_startedAt = -1;
    bool m_wasRunning = false;
};

} // namespace battery
