#pragma once

#include "chartbase.h"

#include <QPointer>

// Several sensors on one time axis. Their units differ, so each line is
// scaled to its own minimum and maximum; the focused one gets its value
// axis drawn in its color.
class OverlayChart : public ChartBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QList<QObject *> series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(int focusIndex READ focusIndex WRITE setFocusIndex NOTIFY focusIndexChanged)

public:
    explicit OverlayChart(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QList<QObject *> series() const;
    void setSeries(const QList<QObject *> &series);

    int focusIndex() const { return m_focusIndex; }
    void setFocusIndex(int index);

signals:
    void seriesChanged();
    void focusIndexChanged();

protected:
    double clockNow() const override;
    bool hasSeries() const override;

private:
    QList<QPointer<SensorSeries>> m_series;
    int m_focusIndex = 0;
};
