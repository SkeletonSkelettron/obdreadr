#pragma once

#include "chartbase.h"

#include <QPointer>

// A scrolling time chart of one SensorSeries, with its own value axis.
class LineChart : public ChartBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(SensorSeries *series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged)

public:
    explicit LineChart(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    SensorSeries *series() const { return m_series; }
    void setSeries(SensorSeries *series);

    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &color);

signals:
    void seriesChanged();
    void lineColorChanged();

protected:
    double clockNow() const override { return m_series ? m_series->now() : 0; }
    bool hasSeries() const override { return !m_series.isNull(); }

private:
    QPointer<SensorSeries> m_series;
    QColor m_lineColor = QColor(0x80, 0xDE, 0xEA);
};
