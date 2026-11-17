#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// The car's own self-tests: which monitors have run since the codes were
// last cleared (mode 01 PID 01), and their measured results (mode 06).
namespace obd {

struct Monitor {
    QString name;
    bool supported;
    bool complete;
};

// Readiness monitors from the four data bytes of mode 01 PID 01.
QList<Monitor> readiness(const QByteArray &pid01);

struct TestResult {
    quint8 mid;      // which monitor: catalyst, O2 sensor, misfire, ...
    quint8 tid;      // which test of that monitor
    quint8 uasid;    // unit and scaling of the numbers
    double value;
    double min;
    double max;
    QString unit;
    int decimals;
    bool hasLimits;  // false for counts and other data without limits
    bool passed;     // value within [min, max]; true when there are no limits
};

// Test results from a mode 06 answer of a CAN car (records of nine bytes:
// MID, TID, unit and scaling, value, minimum and maximum).
QList<TestResult> parseTests(const QStringList &lines);

QString monitorName(quint8 mid);
QString testName(quint8 mid, quint8 tid);

} // namespace obd
