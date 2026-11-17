#include "linechart.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPolygonF>

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
        return LineChart::tr("now");
    if (seconds < 60)
        return QStringLiteral("-%1s").arg(qRound(seconds));
    return QStringLiteral("-%1m").arg(QLocale::c().toString(seconds / 60, 'g', 2));
}

} // namespace

LineChart::LineChart(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, [this] { update(); });
}

void LineChart::setSeries(SensorSeries *series)
{
    if (m_series == series)
        return;
    disconnect(m_seriesConnection);
    m_series = series;
    if (series)
        m_seriesConnection = connect(series, &SensorSeries::changed, this, [this] { update(); });
    updateTicking();
    update();
    emit seriesChanged();
}

void LineChart::setTimeSpan(double seconds)
{
    seconds = std::max(5.0, seconds);
    if (qFuzzyCompare(m_timeSpan, seconds))
        return;
    m_timeSpan = seconds;
    update();
    emit timeSpanChanged();
}

void LineChart::setLive(bool live)
{
    if (m_live == live)
        return;
    if (!live)
        m_frozenAt = m_series ? m_series->now() : 0;
    m_live = live;
    updateTicking();
    update();
    emit liveChanged();
}

void LineChart::setLineColor(const QColor &color)
{
    if (m_lineColor == color)
        return;
    m_lineColor = color;
    update();
    emit colorsChanged();
}

void LineChart::setGridColor(const QColor &color)
{
    if (m_gridColor == color)
        return;
    m_gridColor = color;
    update();
    emit colorsChanged();
}

void LineChart::setTextColor(const QColor &color)
{
    if (m_textColor == color)
        return;
    m_textColor = color;
    update();
    emit colorsChanged();
}

void LineChart::updateTicking()
{
    // Repaint a few times a second so the time axis scrolls smoothly even
    // when a sensor is only sampled now and then.
    if (m_live && m_series)
        m_tick.start();
    else
        m_tick.stop();
}

void LineChart::paint(QPainter *p)
{
    const QRectF bounds(0, 0, width(), height());
    if (bounds.width() < 40 || bounds.height() < 30)
        return;

    p->setRenderHint(QPainter::Antialiasing, true);
    QFont font = p->font();
    font.setPixelSize(11);
    p->setFont(font);
    const QFontMetricsF fm(font);

    const SensorSeries *series = m_series;
    static const std::deque<SensorSeries::Sample> kEmpty;
    const auto &samples = series ? series->samples() : kEmpty;

    const double right = m_live && series ? series->now() : m_frozenAt;
    const double left = right - m_timeSpan;

    auto first = std::lower_bound(samples.begin(), samples.end(), left,
                                  [](const SensorSeries::Sample &s, double t) { return s.t < t; });
    if (first != samples.begin())
        --first; // so the line reaches the left edge
    const auto last = std::upper_bound(first, samples.end(), right,
                                       [](double t, const SensorSeries::Sample &s) { return t < s.t; });

    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (auto it = first; it != last; ++it) {
        lo = std::min(lo, it->v);
        hi = std::max(hi, it->v);
    }
    const bool hasData = first != last;
    if (!hasData) {
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
    lo -= pad;
    hi += pad;

    // Value axis.
    const double step = niceStep((hi - lo) / 4);
    const int decimals = std::clamp(int(-std::floor(std::log10(step))), 0, 3);
    QList<double> ticks;
    QStringList labels;
    for (double v = std::ceil(lo / step) * step; v <= hi + step * 1e-6; v += step) {
        ticks << v;
        labels << QLocale::c().toString(std::abs(v) < step * 1e-6 ? 0.0 : v, 'f', decimals);
    }
    double labelWidth = 0;
    for (const QString &label : std::as_const(labels))
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(label));

    const QRectF plot = bounds.adjusted(labelWidth + 8, 6, -8, -(fm.height() + 4));
    const auto mapX = [&](double t) { return plot.left() + (t - left) / m_timeSpan * plot.width(); };
    const auto mapY = [&](double v) { return plot.bottom() - (v - lo) / (hi - lo) * plot.height(); };

    p->setPen(QPen(m_gridColor, 1));
    for (qsizetype i = 0; i < ticks.size(); ++i) {
        const double y = mapY(ticks[i]);
        p->setPen(QPen(m_gridColor, 1));
        p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p->setPen(m_textColor);
        p->drawText(QRectF(0, y - fm.height() / 2, labelWidth + 2, fm.height()),
                    Qt::AlignRight | Qt::AlignVCenter, labels[i]);
    }

    // Time axis: five marks from the left edge to now.
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

    if (!hasData) {
        p->setPen(m_textColor);
        p->drawText(plot, Qt::AlignCenter, tr("waiting for data…"));
        return;
    }

    // Split the line where samples are missing (paused, reconnecting).
    const double gap = std::max(5.0, m_timeSpan / 20);
    QList<QPolygonF> segments;
    QPolygonF current;
    double previousT = 0;
    for (auto it = first; it != last; ++it) {
        if (!current.isEmpty() && it->t - previousT > gap) {
            segments << current;
            current.clear();
        }
        current << QPointF(mapX(it->t), mapY(it->v));
        previousT = it->t;
    }
    if (!current.isEmpty())
        segments << current;

    p->save();
    p->setClipRect(plot.adjusted(-2, -2, 2, 2));

    QColor top = m_lineColor;
    top.setAlphaF(0.25f);
    QColor bottom = m_lineColor;
    bottom.setAlphaF(0.0f);
    QLinearGradient fill(plot.topLeft(), plot.bottomLeft());
    fill.setColorAt(0, top);
    fill.setColorAt(1, bottom);

    const QPen linePen(m_lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (const QPolygonF &segment : std::as_const(segments)) {
        if (segment.size() >= 2) {
            QPolygonF area = segment;
            area << QPointF(segment.last().x(), plot.bottom()) << QPointF(segment.first().x(), plot.bottom());
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawPolygon(area);
        }
        p->setPen(linePen);
        p->setBrush(Qt::NoBrush);
        p->drawPolyline(segment);
    }

    p->setPen(Qt::NoPen);
    p->setBrush(m_lineColor);
    p->drawEllipse(segments.last().last(), 3.5, 3.5);
    p->restore();
}
