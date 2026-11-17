#include "fuel.h"

namespace fuel {

namespace {

constexpr int kSpeed = 0x0D;
constexpr int kMap = 0x0B;
constexpr int kRpm = 0x0C;
constexpr int kIntakeTemp = 0x0F;
constexpr int kMaf = 0x10;
constexpr int kLambda = 0x44;
constexpr int kFuelRate = 0x5E;

constexpr double kAirMolarMass = 28.97; // g/mol
constexpr double kGasConstant = 8.314;  // J/(mol·K)
constexpr double kMinKm = 0.1;          // distance needed for an average

} // namespace

Properties properties(Kind kind)
{
    switch (kind) {
    case Kind::Petrol: return {14.7, 740};
    case Kind::Diesel: return {14.5, 832};
    case Kind::Lpg: return {15.5, 540};
    }
    return {14.7, 740};
}

Method chooseMethod(const QSet<int> &pids, Kind kind)
{
    if (!pids.contains(kSpeed))
        return Method::None;
    if (pids.contains(kFuelRate))
        return Method::FuelRate;
    if (kind == Kind::Diesel)
        return Method::None;
    if (pids.contains(kMaf))
        return Method::Maf;
    if (pids.contains(kMap) && pids.contains(kRpm))
        return Method::SpeedDensity;
    return Method::None;
}

QList<int> inputs(Method method, const QSet<int> &pids)
{
    QList<int> wanted;
    switch (method) {
    case Method::None: break; // distance still counts
    case Method::FuelRate: wanted = {kFuelRate}; break;
    case Method::Maf: wanted = {kMaf, kLambda}; break;
    case Method::SpeedDensity: wanted = {kMap, kRpm, kIntakeTemp}; break;
    }
    wanted << kSpeed;
    QList<int> result;
    for (int pid : std::as_const(wanted)) {
        if (pids.contains(pid))
            result << pid;
    }
    return result;
}

double litersPerHour(double airGramsPerSecond, Kind kind, double lambda)
{
    const Properties p = properties(kind);
    if (lambda < 0.5 || lambda > 2.0)
        lambda = 1.0; // not a commanded mixture an engine runs
    return airGramsPerSecond * 3600.0 / (p.airFuelRatio * lambda * p.gramsPerLiter);
}

double speedDensityAir(double kpa, double rpm, double intakeC, double liters, double efficiency)
{
    if (rpm <= 0 || kpa <= 0 || liters <= 0)
        return 0;
    // A four-stroke engine fills its displacement once every two turns;
    // the ideal gas law gives the mass of that air.
    const double cubicMetersPerSecond = liters / 1000.0 * rpm / 120.0;
    const double kelvin = intakeC + 273.15;
    const double moles = kpa * 1000.0 * cubicMetersPerSecond / (kGasConstant * kelvin);
    return moles * kAirMolarMass * efficiency;
}

Integrator::Step Integrator::add(double t, double value)
{
    Step step;
    if (m_has) {
        const double dt = t - m_t;
        if (dt > 0 && dt <= m_maxGap) {
            step.area = (m_value + value) / 2.0 * dt;
            step.seconds = dt;
        }
    }
    m_has = true;
    m_t = t;
    m_value = value;
    return step;
}

double Totals::per100km(double correction) const
{
    if (km < kMinKm)
        return -1;
    return liters * correction / km * 100.0;
}

} // namespace fuel
