#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "sensorseries.h"

// A scrolling time chart of one SensorSeries.
class LineChart : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(SensorSeries *series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(double timeSpan READ timeSpan WRITE setTimeSpan NOTIFY timeSpanChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)

public:
    explicit LineChart(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    SensorSeries *series() const { return m_series; }
    void setSeries(SensorSeries *series);

    double timeSpan() const { return m_timeSpan; }
    void setTimeSpan(double seconds);

    // While live the right edge follows the clock; otherwise it stays put.
    bool live() const { return m_live; }
    void setLive(bool live);

    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &color);
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &color);
    QColor textColor() const { return m_textColor; }
    void setTextColor(const QColor &color);

signals:
    void seriesChanged();
    void timeSpanChanged();
    void liveChanged();
    void colorsChanged();

private:
    void updateTicking();

    QPointer<SensorSeries> m_series;
    QMetaObject::Connection m_seriesConnection;
    double m_timeSpan = 60;
    bool m_live = true;
    double m_frozenAt = 0;
    QColor m_lineColor = QColor(0x4d, 0xb6, 0xac);
    QColor m_gridColor = QColor(255, 255, 255, 30);
    QColor m_textColor = QColor(255, 255, 255, 140);
    QTimer m_tick;
};
