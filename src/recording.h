#pragma once

#include "fuel.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>

#include <optional>
#include <vector>

// Reading back a recording (the CSV files Recorder writes) and summing it up.
namespace recording {

struct Point {
    double t;
    double v;
};

struct Column {
    QString name;
    QString unit;
    int pid;                   // mode 01 PID, -1 the adapter voltage, -2 unknown
    std::vector<Point> points; // in time order
};

struct Data {
    QDateTime start;           // wall time of t = 0
    double firstTime = 0;      // seconds of the first and last rows
    double lastTime = 0;
    QList<double> rowTimes;    // every row, for finding gaps
    QList<Column> columns;
};

// Parses a recording; nullopt (and `error`) if it is not one.
std::optional<Data> parse(const QByteArray &csv, QString *error = nullptr);

struct FuelSettings {
    fuel::Kind kind = fuel::Kind::Petrol;
    double engineLiters = 1.6;
    double correction = 1.0;
    double price = 0;          // lari per litre, 0 for no cost
};

// Label and value pairs: duration, gaps, distance, fuel, average and cost,
// top speed and engine speed, highest coolant temperature, voltage range,
// warm idle speed and average fuel trim, as far as the recording has them.
QList<QPair<QString, QString>> summarize(const Data &data, const FuelSettings &settings);

} // namespace recording
