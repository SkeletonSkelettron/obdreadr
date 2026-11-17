#pragma once

#include <QList>
#include <QSet>

// Fuel consumption from live sensor data: the car's own fuel rate (PID 5E)
// when it reports one, otherwise an estimate from the air the engine takes
// in, either measured by the MAF sensor (PID 10) or worked out from intake
// manifold pressure, engine speed and intake air temperature (speed-density).
namespace fuel {

enum class Kind { Petrol, Diesel, Lpg };
enum class Method { None, FuelRate, Maf, SpeedDensity };

struct Properties {
    double airFuelRatio;  // stoichiometric, by mass
    double gramsPerLiter; // density of the liquid fuel
};
Properties properties(Kind kind);

// The best method for a car that reports `pids`. The airflow estimates
// assume a stoichiometric mixture, which a diesel does not run, so a diesel
// needs the fuel rate PID.
Method chooseMethod(const QSet<int> &pids, Kind kind);

// The supported PIDs a method reads, vehicle speed included (also without
// a method, for the distance).
QList<int> inputs(Method method, const QSet<int> &pids);

// Liters per hour burnt with `airGramsPerSecond` of air at commanded `lambda`.
double litersPerHour(double airGramsPerSecond, Kind kind, double lambda = 1.0);

// Air mass flow (g/s) into a four-stroke engine: manifold pressure (kPa),
// engine speed (rpm), intake temperature (°C), displacement (L) and
// volumetric efficiency.
double speedDensityAir(double kpa, double rpm, double intakeC, double liters, double efficiency = 0.85);

// Integrates a quantity sampled at irregular times with the trapezoid rule.
// A gap longer than maxGap seconds (paused, reconnecting) adds nothing.
class Integrator
{
public:
    struct Step {
        double area = 0;    // value × seconds
        double seconds = 0; // time covered
    };

    explicit Integrator(double maxGap = 10) : m_maxGap(maxGap) {}

    Step add(double t, double value);
    void reset() { m_has = false; }

private:
    double m_maxGap;
    bool m_has = false;
    double m_t = 0;
    double m_value = 0;
};

// Distance, fuel and time of a stretch of driving.
struct Totals {
    double km = 0;
    double liters = 0;
    double seconds = 0;

    // Liters per 100 km, negative until there is enough distance to say.
    double per100km(double correction = 1.0) const;
};

} // namespace fuel
