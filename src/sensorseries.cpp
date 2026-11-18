#include "sensorseries.h"

#include <QElapsedTimer>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <iterator>


SensorSeries::SensorSeries(const QString &name, const QString &unit, int decimals, double nominalMin,
                           double nominalMax, const QColor &color, const QElapsedTimer *clock,
                           QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_unit(unit)
    , m_decimals(decimals)
    , m_nominalMin(nominalMin)
    , m_nominalMax(nominalMax)
    , m_color(color)
    , m_clock(clock)
{
}

QColor SensorSeries::paletteColor(int index)
{
    // Material 200 tones, ordered so that consecutive sensors differ in hue.
    static const QColor kPalette[] = {
        QColor(0x80, 0xDE, 0xEA), // cyan
        QColor(0xFF, 0xCC, 0x80), // orange
        QColor(0xA5, 0xD6, 0xA7), // green
        QColor(0xF4, 0x8F, 0xB1), // pink
        QColor(0x90, 0xCA, 0xF9), // blue
        QColor(0xFF, 0xF5, 0x9D), // yellow
        QColor(0xCE, 0x93, 0xD8), // purple
        QColor(0x80, 0xCB, 0xC4), // teal
        QColor(0xFF, 0xAB, 0x91), // deep orange
        QColor(0xC5, 0xE1, 0xA5), // light green
        QColor(0x9F, 0xA8, 0xDA), // indigo
        QColor(0xE6, 0xEE, 0x9C), // lime
        QColor(0xB3, 0x9D, 0xDB), // deep purple
        QColor(0x81, 0xD4, 0xFA), // light blue
        QColor(0xFF, 0xE0, 0x82), // amber
        QColor(0xEF, 0x9A, 0x9A), // red
    };
    const int count = int(std::size(kPalette));
    return kPalette[((index % count) + count) % count];
}

QString SensorSeries::format(double v) const
{
    return QLocale::c().toString(v, 'f', m_decimals);
}

QString SensorSeries::valueText() const
{
    if (m_samples.empty())
        return QStringLiteral("–");
    return format(m_samples.back().v);
}

QString SensorSeries::rangeText() const
{
    if (m_samples.empty())
        return QString();
    const double from = now() - m_rangeWindow;
    double lo = m_samples.back().v;
    double hi = lo;
    for (auto it = m_samples.rbegin(); it != m_samples.rend() && it->t >= from; ++it) {
        lo = std::min(lo, it->v);
        hi = std::max(hi, it->v);
    }
    return format(lo) + QStringLiteral(" – ") + format(hi);
}

void SensorSeries::setRangeWindow(double seconds)
{
    if (m_rangeWindow == seconds)
        return;
    m_rangeWindow = seconds;
    emit changed();
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
    while (!m_samples.empty() && m_samples.front().t < t - m_retention)
        m_samples.pop_front();
    emit changed();
}
