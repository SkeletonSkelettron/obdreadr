#include "accel.h"

#include <algorithm>

namespace accel {

void Timer::reset()
{
    *this = Timer();
}

double Timer::elapsed(double now) const
{
    switch (m_state) {
    case State::Running: return std::max(0.0, now - m_start);
    case State::Done:
    case State::Aborted: return std::max(0.0, m_end - m_start);
    default: return 0;
    }
}

void Timer::addSpeed(double t, double kmh)
{
    const bool still = kmh <= kStandstill;
    switch (m_state) {
    case State::Idle:
    case State::Done:
    case State::Aborted:
        // Standing again arms a new run; the last results stay until it starts.
        if (still) {
            m_state = State::Ready;
            m_lastStill = t;
        }
        break;
    case State::Ready:
        if (still) {
            m_lastStill = t;
        } else {
            m_state = State::Running;
            m_start = m_lastStill;
            m_end = -1;
            m_time60.reset();
            m_time100.reset();
            m_prevT = m_lastStill;
            m_prevV = 0;
            m_top = 0;
        }
        break;
    case State::Running:
        break;
    }
    if (m_state != State::Running)
        return;

    // A crossing of `target` between the previous reading and this one.
    const auto cross = [&](double target) {
        if (m_prevV < target && kmh >= target && kmh > m_prevV)
            return m_prevT + (target - m_prevV) / (kmh - m_prevV) * (t - m_prevT) - m_start;
        return -1.0;
    };
    if (!m_time60) {
        const double at = cross(60);
        if (at >= 0)
            m_time60 = at;
    }
    const double at100 = cross(100);
    if (at100 >= 0) {
        m_time100 = at100;
        m_state = State::Done;
        m_end = m_start + at100;
    } else if (still && t > m_start) {
        m_state = State::Ready; // stopped again before 100: a new run may start
        m_lastStill = t;
    } else if (kmh < m_top - kSlowDown || t - m_start > kGiveUpSeconds) {
        m_state = State::Aborted;
        m_end = t;
    }
    m_top = std::max(m_top, kmh);
    m_prevT = t;
    m_prevV = kmh;
}

} // namespace accel
