#pragma once

#include <optional>

// The 0–60 and 0–100 km/h timer. Fed vehicle speed readings: it arms when
// the car stands, starts at the last reading at standstill once the car
// moves, and stops at 100 km/h. The crossings are interpolated between
// readings, which come a few times a second at best.
namespace accel {

enum class State {
    Idle,    // waiting for the car to stand still
    Ready,   // standing: the run starts when the car moves
    Running,
    Done,    // reached 100 km/h
    Aborted, // slowed down or took too long
};

class Timer
{
public:
    static constexpr double kStandstill = 0.5;   // km/h
    static constexpr double kGiveUpSeconds = 60;
    static constexpr double kSlowDown = 5;       // km/h below the run's top speed

    void reset();
    void addSpeed(double t, double kmh);

    State state() const { return m_state; }
    // Seconds since the start of a run (0 before one).
    double elapsed(double now) const;
    std::optional<double> time60() const { return m_time60; }
    std::optional<double> time100() const { return m_time100; }

private:
    State m_state = State::Idle;
    double m_lastStill = 0;  // the latest reading at standstill
    double m_start = 0;
    double m_end = -1;       // when the run ended
    double m_prevT = 0;
    double m_prevV = 0;
    double m_top = 0;
    std::optional<double> m_time60;
    std::optional<double> m_time100;
};

} // namespace accel
