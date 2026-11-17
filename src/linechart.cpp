#include "linechart.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>

LineChart::LineChart(QQuickItem *parent)
    : ChartBase(parent)
{
}

void LineChart::setSeries(SensorSeries *series)
{
    if (m_series == series)
        return;
    m_series = series;
    watch({series});
    emit seriesChanged();
}

void LineChart::setLineColor(const QColor &color)
{
    if (m_lineColor == color)
        return;
    m_lineColor = color;
    requestRepaint();
    emit lineColorChanged();
}

void LineChart::paint(QPainter *p)
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
    static const Samples kEmpty;
    const Visible visible = m_series ? visibleSamples(*m_series, left, right) : Visible{kEmpty.end(), kEmpty.end()};
    const ValueAxis axis = valueAxis(m_series, visible);

    double labelWidth = 0;
    for (const QString &label : axis.labels)
        labelWidth = std::max(labelWidth, fm.horizontalAdvance(label));
    const QRectF plot = bounds.adjusted(labelWidth + 8, 6, -8, -(fm.height() + 4));

    for (qsizetype i = 0; i < axis.ticks.size(); ++i) {
        const double y = plot.bottom() - (axis.ticks[i] - axis.lo) / (axis.hi - axis.lo) * plot.height();
        p->setPen(QPen(gridColor(), 1));
        p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p->setPen(textColor());
        p->drawText(QRectF(0, y - fm.height() / 2, labelWidth + 2, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                    axis.labels[i]);
    }
    drawTimeAxis(p, plot, bounds, fm);

    if (visible.empty()) {
        p->setPen(textColor());
        p->drawText(plot, Qt::AlignCenter, tr("waiting for data…"));
        return;
    }
    drawLine(p, segments(visible, left, plot, axis.lo, axis.hi), m_lineColor, plot, 2, true);
}
