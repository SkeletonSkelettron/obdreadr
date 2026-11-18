#pragma once

#include <QColor>
#include <QList>
#include <QPolygonF>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "sensorseries.h"

class QFontMetricsF;

// What LineChart and OverlayChart share: a scrolling time axis, the
// colors, and repainting only while the chart is on screen.
class ChartBase : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(double timeSpan READ timeSpan WRITE setTimeSpan NOTIFY timeSpanChanged)
    Q_PROPERTY(bool live READ live WRITE setLive NOTIFY liveChanged)
    Q_PROPERTY(double endTime READ endTime WRITE setEndTime NOTIFY endTimeChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)

public:
    explicit ChartBase(QQuickItem *parent = nullptr);

    double timeSpan() const { return m_timeSpan; }
    void setTimeSpan(double seconds);

    // While live the right edge follows the clock; otherwise it stays put.
    bool live() const { return m_live; }
    void setLive(bool live);

    // When 0 or more, the right edge stays at this time (a recording).
    double endTime() const { return m_endTime; }
    void setEndTime(double seconds);

    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &color);
    QColor textColor() const { return m_textColor; }
    void setTextColor(const QColor &color);

signals:
    void timeSpanChanged();
    void liveChanged();
    void endTimeChanged();
    void colorsChanged();

protected:
    using Samples = std::deque<SensorSeries::Sample>;

    struct Visible {
        Samples::const_iterator first;
        Samples::const_iterator last;
        bool empty() const { return first == last; }
    };

    struct ValueAxis {
        double lo = 0;
        double hi = 1;
        QList<double> ticks;
        QStringList labels;
    };

    // Seconds on the session clock, from the first series there is.
    virtual double clockNow() const { return 0; }
    virtual bool hasSeries() const { return false; }

    double rightEdge() const;
    Visible visibleSamples(const SensorSeries &series, double left, double right) const;
    static ValueAxis valueAxis(const SensorSeries *series, const Visible &visible);
    // The line split where samples are missing (paused, reconnecting).
    QList<QPolygonF> segments(const Visible &visible, double left, const QRectF &plot, double lo,
                              double hi) const;
    void drawTimeAxis(QPainter *p, const QRectF &plot, const QRectF &bounds, const QFontMetricsF &fm) const;
    static void drawLine(QPainter *p, const QList<QPolygonF> &segments, const QColor &color,
                         const QRectF &plot, qreal width, bool fill);
    static QFont labelFont(const QFont &base);

    // Repaint when a series changes; does nothing while hidden.
    void watch(const QList<SensorSeries *> &series);
    void requestRepaint();
    void updateTicking();

private:
    double m_timeSpan = 60;
    bool m_live = true;
    double m_frozenAt = 0;
    double m_endTime = -1;
    QColor m_gridColor = QColor(255, 255, 255, 30);
    QColor m_textColor = QColor(255, 255, 255, 140);
    QTimer m_tick;
    QList<QMetaObject::Connection> m_watched;
};
