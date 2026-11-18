#include "battery.h"

#include <algorithm>

namespace battery {

namespace {

double median(std::vector<double> values)
{
    if (values.empty())
        return -1;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

} // namespace

Test::Test(double startTime)
    : m_start(startTime)
{
}

void Test::tick(double t)
{
    // In order, so that one late tick can pass several phases.
    if (m_phase == Phase::Resting && t - m_start >= kRestSeconds)
        m_phase = Phase::Crank;
    if (m_phase == Phase::Crank && t - m_start >= kRestSeconds + kGiveUpSeconds)
        m_phase = Phase::Done;
    if (m_phase == Phase::Running && t - m_startedAt >= kChargeTo)
        m_phase = Phase::Done;
}

void Test::addVolts(double t, double volts)
{
    tick(t);
    switch (m_phase) {
    case Phase::Resting:
        m_rest.push_back(volts);
        break;
    case Phase::Crank:
        m_crankMin = m_crankMin < 0 ? volts : std::min(m_crankMin, volts);
        break;
    case Phase::Running:
        // The starter may still turn for a moment after the engine fires.
        if (t - m_startedAt < 0.5)
            m_crankMin = m_crankMin < 0 ? volts : std::min(m_crankMin, volts);
        else if (t - m_startedAt >= kChargeFrom)
            m_charge.push_back(volts);
        break;
    case Phase::Done:
        break;
    }
}

void Test::addRpm(double t, double rpm)
{
    tick(t);
    if (rpm <= kStartRpm)
        return;
    if (m_phase == Phase::Resting) {
        m_wasRunning = true;
        m_phase = Phase::Done;
    } else if (m_phase == Phase::Crank) {
        m_startedAt = t;
        m_phase = Phase::Running;
    }
}

double Test::rest() const
{
    return median(m_rest);
}

double Test::charging() const
{
    return median(m_charge);
}

Verdict Test::restVerdict(double volts)
{
    if (volts < 0)
        return Verdict::None;
    return volts >= 12.4 ? Verdict::Ok : volts >= 12.2 ? Verdict::Watch : Verdict::Bad;
}

Verdict Test::crankVerdict(double volts)
{
    if (volts < 0)
        return Verdict::None;
    return volts >= 10.0 ? Verdict::Ok : volts >= 9.6 ? Verdict::Watch : Verdict::Bad;
}

Verdict Test::chargeVerdict(double volts)
{
    if (volts < 0)
        return Verdict::None;
    if (volts >= 13.5 && volts <= 14.8)
        return Verdict::Ok;
    if (volts >= 13.0 && volts <= 15.2)
        return Verdict::Watch;
    return Verdict::Bad;
}

} // namespace battery
