#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <deque>

class QElapsedTimer;

// The recent history of one sensor, shown by a LineChart.
class SensorSeries : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Sensor series are created by ObdSession")

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString unit READ unit CONSTANT)
    Q_PROPERTY(bool hasValue READ hasValue NOTIFY changed)
    Q_PROPERTY(double value READ value NOTIFY changed)
    Q_PROPERTY(QString valueText READ valueText NOTIFY changed)

public:
    struct Sample {
        double t; // seconds since the session started
        double v;
    };

    SensorSeries(const QString &name, const QString &unit, int decimals, double nominalMin,
                 double nominalMax, const QElapsedTimer *clock, QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString unit() const { return m_unit; }
    bool hasValue() const { return !m_samples.empty(); }
    double value() const { return m_samples.empty() ? 0.0 : m_samples.back().v; }
    QString valueText() const;
    int decimals() const { return m_decimals; }

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
    QString m_name;
    QString m_unit;
    int m_decimals;
    double m_nominalMin;
    double m_nominalMax;
    const QElapsedTimer *m_clock;
    std::deque<Sample> m_samples;
};
