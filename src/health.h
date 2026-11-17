#pragma once

#include <QList>
#include <QString>

#include <functional>

class SensorSeries;

// Checks on the live sensor data for problems that develop before the car
// sets a fault code: fuel trims drifting, a thermostat stuck open, a weak
// alternator, an unsteady idle, a lazy O2 sensor, an ageing catalyst, ...
// The limits are general rules of thumb, not the car maker's values.
namespace health {

enum class Status { Ok, Warn, Bad, Info, NoData };

struct Check {
    QString title;
    Status status;
    QString value;  // what was measured, e.g. "14.1 V"
    QString detail; // what it means and what to look at
};

// The series of a mode 01 PID, or the adapter's battery reading for pid -1;
// nullptr when the car does not report it.
using Lookup = std::function<const SensorSeries *(int pid)>;

// Evaluates every check at session time `now` (seconds).
QList<Check> evaluate(const Lookup &series, double now);

QString statusKey(Status status); // "ok", "warn", "bad", "info", "nodata"

} // namespace health
