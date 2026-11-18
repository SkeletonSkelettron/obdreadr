#include "chartbase.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// 1, 2 or 5 times a power of ten, at least `raw`.
double niceStep(double raw)
{
    if (!(raw > 0))
        return 1;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / magnitude;
    const double nice = f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10;
    return nice * magnitude;
}

QString agoText(double seconds)
{
    if (seconds < 0.5)
        return ChartBase::tr("now");
    if (seconds < 60)
        return QStringLiteral("-%1s").arg(qRound(seconds));
    return QStringLiteral("-%1m").arg(QLocale::c().toString(seconds / 60, 'g', 2));
}

} // namespace

ChartBase::ChartBase(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &ChartBase::requestRepaint);
    connect(this, &QQuickItem::visibleChanged, this, [this] {
        updateTicking();
        requestRepaint();
    });
}

void ChartBase::setTimeSpan(double seconds)
{
    seconds = std::max(5.0, seconds);
    if (qFuzzyCompare(m_timeSpan, seconds))
        return;
    m_timeSpan = seconds;
    requestRepaint();
    emit timeSpanChanged();
}

void ChartBase::setLive(bool live)
{
    if (m_live == live)
        return;
    if (!live)
        m_frozenAt = clockNow();
    m_live = live;
    updateTicking();
    requestRepaint();
    emit liveChanged();
}

void ChartBase::setGridColor(const QColor &color)
{
    if (m_gridColor == color)
        return;
    m_gridColor = color;
    requestRepaint();
    emit colorsChanged();
}

void ChartBase::setTextColor(const QColor &color)
{
    if (m_textColor == color)
        return;
    m_textColor = color;
    requestRepaint();
    emit colorsChanged();
}

void ChartBase::setEndTime(double seconds)
{
    if (qFuzzyCompare(m_endTime + 1, seconds + 1))
        return;
    m_endTime = seconds;
    updateTicking();
    requestRepaint();
    emit endTimeChanged();
}

double ChartBase::rightEdge() const
{
    if (m_endTime >= 0)
        return m_endTime;
    return m_live && hasSeries() ? clockNow() : m_frozenAt;
}

void ChartBase::watch(const QList<SensorSeries *> &series)
{
    for (const QMetaObject::Connection &c : std::as_const(m_watched))
        disconnect(c);
    m_watched.clear();
    for (SensorSeries *s : series) {
        if (s)
            m_watched << connect(s, &SensorSeries::changed, this, &ChartBase::requestRepaint);
    }
    updateTicking();
    requestRepaint();
}

void ChartBase::requestRepaint()
{
    if (isVisible())
        update();
}

void ChartBase::updateTicking()
{
    // Repaint a few times a second so the time axis scrolls smoothly even
    // when a sensor is only sampled now and then.
    if (m_live && m_endTime < 0 && hasSeries() && isVisible())
        m_tick.start();
    else
        m_tick.stop();
}

ChartBase::Visible ChartBase::visibleSamples(const SensorSeries &series, double left, double right) const
{
    const Samples &samples = series.samples();
    auto first = std::lower_bound(samples.begin(), samples.end(), left,
                                  [](const SensorSeries::Sample &s, double t) { return s.t < t; });
    if (first != samples.begin())
        --first; // so the line reaches the left edge
    const auto last = std::upper_bound(first, samples.end(), right,
                                       [](double t, const SensorSeries::Sample &s) { return t < s.t; });
    return {first, last};
}

ChartBase::ValueAxis ChartBase::valueAxis(const SensorSeries *series, const Visible &visible)
{
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (auto it = visible.first; it != visible.last; ++it) {
        lo = std::min(lo, it->v);
        hi = std::max(hi, it->v);
    }
    if (visible.empty()) {
        lo = series ? series->nominalMin() : 0;
        hi = series ? series->nominalMax() : 1;
    }
    const double minSpan = series ? series->minSpan() : 1;
    if (hi - lo < minSpan) {
        const double mid = (hi + lo) / 2;
        lo = mid - minSpan / 2;
        hi = mid + minSpan / 2;
    }
    const double pad = (hi - lo) * 0.08;

    ValueAxis axis;
    axis.lo = lo - pad;
    axis.hi = hi + pad;
    const double step = niceStep((axis.hi - axis.lo) / 4);
    const int decimals = std::clamp(int(-std::floor(std::log10(step))), 0, 3);
    for (double v = std::ceil(axis.lo / step) * step; v <= axis.hi + step * 1e-6; v += step) {
        axis.ticks << v;
        axis.labels << QLocale::c().toString(std::abs(v) < step * 1e-6 ? 0.0 : v, 'f', decimals);
    }
    return axis;
}

QList<QPolygonF> ChartBase::segments(const Visible &visible, double left, const QRectF &plot, double lo,
                                     double hi) const
{
    const double gap = std::max(5.0, m_timeSpan / 20);
    QList<QPolygonF> result;
    QPolygonF current;
    double previousT = 0;
    for (auto it = visible.first; it != visible.last; ++it) {
        if (!current.isEmpty() && it->t - previousT > gap) {
            result << current;
            current.clear();
        }
        const double x = plot.left() + (it->t - left) / m_timeSpan * plot.width();
        const double y = plot.bottom() - (it->v - lo) / (hi - lo) * plot.height();
        current << QPointF(x, y);
        previousT = it->t;
    }
    if (!current.isEmpty())
        result << current;
    return result;
}

void ChartBase::drawTimeAxis(QPainter *p, const QRectF &plot, const QRectF &bounds, const QFontMetricsF &fm) const
{
    for (int i = 0; i <= 4; ++i) {
        const double x = plot.left() + plot.width() * i / 4;
        p->setPen(QPen(m_gridColor, 1));
        p->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const QString label = agoText(m_timeSpan * (4 - i) / 4);
        const double w = fm.horizontalAdvance(label);
        double lx = x - w / 2;
        if (i == 0)
            lx = x;
        else if (i == 4)
            lx = x - w;
        p->setPen(m_textColor);
        p->drawText(QPointF(lx, bounds.bottom() - fm.descent() - 1), label);
    }
}

void ChartBase::drawLine(QPainter *p, const QList<QPolygonF> &segments, const QColor &color, const QRectF &plot,
                         qreal width, bool fill)
{
    if (segments.isEmpty())
        return;
    p->save();
    p->setClipRect(plot.adjusted(-3, -3, 3, 3));

    if (fill) {
        QColor top = color;
        top.setAlphaF(0.22f);
        QColor bottom = color;
        bottom.setAlphaF(0.0f);
        QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
        gradient.setColorAt(0, top);
        gradient.setColorAt(1, bottom);
        p->setPen(Qt::NoPen);
        p->setBrush(gradient);
        for (const QPolygonF &segment : segments) {
            if (segment.size() < 2)
                continue;
            QPolygonF area = segment;
            area << QPointF(segment.last().x(), plot.bottom()) << QPointF(segment.first().x(), plot.bottom());
            p->drawPolygon(area);
        }
    }

    p->setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    for (const QPolygonF &segment : segments)
        p->drawPolyline(segment);

    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawEllipse(segments.last().last(), width + 1.5, width + 1.5);
    p->restore();
}

QFont ChartBase::labelFont(const QFont &base)
{
    QFont font = base;
    font.setPixelSize(11);
    return font;
}
