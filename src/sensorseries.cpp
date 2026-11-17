#include "sensorseries.h"

#include <QElapsedTimer>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kRetentionSeconds = 15 * 60;
} // namespace

SensorSeries::SensorSeries(const QString &name, const QString &unit, int decimals, double nominalMin,
                           double nominalMax, const QElapsedTimer *clock, QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_unit(unit)
    , m_decimals(decimals)
    , m_nominalMin(nominalMin)
    , m_nominalMax(nominalMax)
    , m_clock(clock)
{
}

QString SensorSeries::valueText() const
{
    if (m_samples.empty())
        return QStringLiteral("–");
    return QLocale::c().toString(m_samples.back().v, 'f', m_decimals);
}

double SensorSeries::minSpan() const
{
    const double step = std::pow(10.0, -m_decimals);
    return std::max((m_nominalMax - m_nominalMin) * 0.02, step * 4);
}

double SensorSeries::now() const
{
    if (m_clock && m_clock->isValid())
        return m_clock->elapsed() / 1000.0;
    return m_samples.empty() ? 0.0 : m_samples.back().t;
}

void SensorSeries::append(double t, double v)
{
    m_samples.push_back({t, v});
    while (!m_samples.empty() && m_samples.front().t < t - kRetentionSeconds)
        m_samples.pop_front();
    emit changed();
}
