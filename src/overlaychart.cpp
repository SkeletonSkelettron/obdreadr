#include "overlaychart.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>

OverlayChart::OverlayChart(QQuickItem *parent)
    : ChartBase(parent)
{
}

QList<QObject *> OverlayChart::series() const
{
    QList<QObject *> list;
    for (const QPointer<SensorSeries> &s : m_series)
        list << s.data();
    return list;
}

void OverlayChart::setSeries(const QList<QObject *> &series)
{
    QList<QPointer<SensorSeries>> list;
    QList<SensorSeries *> raw;
    for (QObject *object : series) {
        if (auto *s = qobject_cast<SensorSeries *>(object)) {
            list << s;
            raw << s;
        }
    }
    if (list == m_series)
        return;
    m_series = list;
    watch(raw);
    emit seriesChanged();
}

void OverlayChart::setFocusIndex(int index)
{
    if (m_focusIndex == index)
        return;
    m_focusIndex = index;
    requestRepaint();
    emit focusIndexChanged();
}

double OverlayChart::clockNow() const
{
    for (const QPointer<SensorSeries> &s : m_series) {
        if (s)
            return s->now();
    }
    return 0;
}

bool OverlayChart::hasSeries() const
{
    return std::any_of(m_series.begin(), m_series.end(), [](const QPointer<SensorSeries> &s) { return !s.isNull(); });
}

void OverlayChart::paint(QPainter *p)
{
    const QRectF bounds(0, 0, width(), height());
    if (bounds.width() < 40 || bounds.height() < 30)
        return;

    p->setRenderHint(QPainter::Antialiasing, true);
    const QFont font = labelFont(p->font());
    p->setFont(font);
    const QFontMetricsF fm(font);

    const double right = rightEdge();
    const double left = right - timeSpan();

    struct Line {
        SensorSeries *series;
        Visible visible;
        ValueAxis axis;
    };
    QList<Line> lines;
    for (const QPointer<SensorSeries> &s : m_series) {
        if (!s)
            continue;
        const Visible visible = visibleSamples(*s, left, right);
        lines << Line{s.data(), visible, valueAxis(s.data(), visible)};
    }
    const qsizetype focus = m_focusIndex >= 0 && m_focusIndex < lines.size() ? m_focusIndex : -1;

    // Only the focused sensor's scale is labelled; the others share its grid.
    double labelWidth = 0;
    if (focus >= 0) {
        for (const QString &label : std::as_const(lines[focus].axis.labels))
            labelWidth = std::max(labelWidth, fm.horizontalAdvance(label));
    }
    const QRectF plot = bounds.adjusted(labelWidth + 8, 6, -8, -(fm.height() + 4));

    if (focus >= 0) {
        const ValueAxis &axis = lines[focus].axis;
        for (qsizetype i = 0; i < axis.ticks.size(); ++i) {
            const double y = plot.bottom() - (axis.ticks[i] - axis.lo) / (axis.hi - axis.lo) * plot.height();
            p->setPen(QPen(gridColor(), 1));
            p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p->setPen(lines[focus].series->color());
            p->drawText(QRectF(0, y - fm.height() / 2, labelWidth + 2, fm.height()),
                        Qt::AlignRight | Qt::AlignVCenter, axis.labels[i]);
        }
    } else {
        for (int i = 0; i <= 4; ++i) {
            const double y = plot.top() + plot.height() * i / 4;
            p->setPen(QPen(gridColor(), 1));
            p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
    }
    drawTimeAxis(p, plot, bounds, fm);

    if (lines.isEmpty()) {
        p->setPen(textColor());
        p->drawText(plot, Qt::AlignCenter, tr("Choose sensors to compare"));
        return;
    }

    // The focused line last, so it is drawn on top.
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (i == focus)
            continue;
        const Line &l = lines[i];
        drawLine(p, segments(l.visible, left, plot, l.axis.lo, l.axis.hi), l.series->color(), plot, 2, false);
    }
    if (focus >= 0) {
        const Line &l = lines[focus];
        drawLine(p, segments(l.visible, left, plot, l.axis.lo, l.axis.hi), l.series->color(), plot, 3, true);
    }
}
