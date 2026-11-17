#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <deque>

class QElapsedTimer;

// The recent history of one sensor, shown by the charts and value views.
class SensorSeries : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Sensor series are created by ObdSession")

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString unit READ unit CONSTANT)
    Q_PROPERTY(QColor color READ color CONSTANT)
    Q_PROPERTY(bool hasValue READ hasValue NOTIFY changed)
    Q_PROPERTY(double value READ value NOTIFY changed)
    Q_PROPERTY(QString valueText READ valueText NOTIFY changed)
    Q_PROPERTY(QString rangeText READ rangeText NOTIFY changed)

public:
    struct Sample {
        double t; // seconds since the session started
        double v;
    };

    SensorSeries(const QString &name, const QString &unit, int decimals, double nominalMin,
                 double nominalMax, const QColor &color, const QElapsedTimer *clock,
                 QObject *parent = nullptr);

    // Light colors that stay readable on the dark theme; neighbours differ in hue.
    static QColor paletteColor(int index);

    QString name() const { return m_name; }
    QString unit() const { return m_unit; }
    QColor color() const { return m_color; }
    bool hasValue() const { return !m_samples.empty(); }
    double value() const { return m_samples.empty() ? 0.0 : m_samples.back().v; }
    QString valueText() const;
    int decimals() const { return m_decimals; }

    // "min – max" of the last rangeWindow() seconds.
    QString rangeText() const;
    double rangeWindow() const { return m_rangeWindow; }
    void setRangeWindow(double seconds);

    double nominalMin() const { return m_nominalMin; }
    double nominalMax() const { return m_nominalMax; }
    // The smallest value range a chart shows, so noise does not fill it.
    double minSpan() const;

    // Seconds since the session started.
    double now() const;

    void append(double t, double v);
    const std::deque<Sample> &samples() const { return m_samples; }

signals:
    void changed();

private:
    QString format(double v) const;

    QString m_name;
    QString m_unit;
    int m_decimals;
    double m_nominalMin;
    double m_nominalMax;
    QColor m_color;
    const QElapsedTimer *m_clock;
    double m_rangeWindow = 60;
    std::deque<Sample> m_samples;
};
