#include "health.h"

#include "sensorseries.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace health {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Health", text);
}

QString num(double v, int decimals)
{
    return QLocale::c().toString(v, 'f', decimals);
}

QString signedNum(double v, int decimals)
{
    return (v > 0 ? QStringLiteral("+") : QString()) + num(v, decimals);
}

double mean(const std::vector<double> &v)
{
    double sum = 0;
    for (double x : v)
        sum += x;
    return v.empty() ? 0 : sum / double(v.size());
}

double stddev(const std::vector<double> &v)
{
    const double m = mean(v);
    double sum = 0;
    for (double x : v)
        sum += (x - m) * (x - m);
    return v.empty() ? 0 : std::sqrt(sum / double(v.size()));
}

double median(std::vector<double> v)
{
    if (v.empty())
        return 0;
    const auto middle = v.begin() + qsizetype(v.size() / 2);
    std::nth_element(v.begin(), middle, v.end());
    return *middle;
}

constexpr int kAdapterVoltage = -1;
constexpr int kLoad = 0x04;
constexpr int kCoolant = 0x05;
constexpr int kRpm = 0x0C;
constexpr int kSpeed = 0x0D;
constexpr int kTiming = 0x0E;
constexpr int kIntakeAir = 0x0F;
constexpr int kMaf = 0x10;
constexpr int kRunTime = 0x1F;
constexpr int kModuleVoltage = 0x42;
constexpr int kAmbient = 0x46;

class Evaluator
{
public:
    Evaluator(const Lookup &lookup, double now)
        : m_lookup(lookup)
        , m_now(now)
        , m_rpm(lookup(kRpm))
        , m_speed(lookup(kSpeed))
        , m_coolant(lookup(kCoolant))
    {
    }

    QList<Check> run()
    {
        QList<Check> checks;
        checks << charging();
        const std::optional<double> bank1 = fuelTrim(checks, 1, 0x06, 0x07);
        const std::optional<double> bank2 = fuelTrim(checks, 2, 0x08, 0x09);
        if (bank1 && bank2)
            checks << bankBalance(*bank1, *bank2);
        checks << coolant() << coldStart() << idleSpeed() << idleAirflow() << idleLoad();
        oxygenSensor(checks, 1, 0x14);
        oxygenSensor(checks, 2, 0x18);
        catalyst(checks, 1, 0x14, 0x15);
        catalyst(checks, 2, 0x18, 0x19);
        checks << knock() << intakeHeat();
        return checks;
    }

private:
    const SensorSeries *series(int pid) const { return m_lookup(pid); }

    // Samples of the last `seconds`.
    std::vector<SensorSeries::Sample> recent(const SensorSeries *s, double seconds) const
    {
        if (!s)
            return {};
        const auto &all = s->samples();
        const auto first = std::lower_bound(all.begin(), all.end(), m_now - seconds,
                                            [](const SensorSeries::Sample &a, double t) { return a.t < t; });
        return std::vector<SensorSeries::Sample>(first, all.end());
    }

    // The last value at or before t, if it is at most maxAge seconds old.
    static std::optional<double> at(const SensorSeries *s, double t, double maxAge = 3.0)
    {
        if (!s)
            return std::nullopt;
        const auto &all = s->samples();
        auto it = std::upper_bound(all.begin(), all.end(), t,
                                   [](double time, const SensorSeries::Sample &a) { return time < a.t; });
        if (it == all.begin())
            return std::nullopt;
        --it;
        if (t - it->t > maxAge)
            return std::nullopt;
        return it->v;
    }

    bool running(double t) const
    {
        const auto rpm = at(m_rpm, t);
        return rpm && *rpm > 400;
    }

    // Warm and running, so the engine controls the mixture in closed loop.
    bool warmRunning(double t) const
    {
        if (!running(t))
            return false;
        if (!m_coolant)
            return true;
        const auto c = at(m_coolant, t, 10);
        return c && *c >= 70;
    }

    bool idling(double t) const
    {
        const auto rpm = at(m_rpm, t);
        if (!rpm || *rpm < 400)
            return false;
        if (m_speed) {
            const auto speed = at(m_speed, t);
            return speed && *speed < 2;
        }
        return *rpm < 1100;
    }

    // Values of `s` in the last `seconds` at moments that pass `when`.
    template <typename Condition>
    std::vector<double> valuesWhen(const SensorSeries *s, double seconds, Condition when) const
    {
        std::vector<double> values;
        for (const auto &sample : recent(s, seconds)) {
            if (when(sample.t))
                values.push_back(sample.v);
        }
        return values;
    }

    double runningSeconds() const
    {
        if (const SensorSeries *runTime = series(kRunTime); runTime && runTime->hasValue())
            return runTime->value();
        double seconds = 0;
        if (!m_rpm)
            return 0;
        const auto &all = m_rpm->samples();
        for (size_t i = 1; i < all.size(); ++i) {
            const double dt = all[i].t - all[i - 1].t;
            if (all[i].v > 400 && dt < 5)
                seconds += dt;
        }
        return seconds;
    }

    static Check noData(const QString &title, const QString &detail) { return {title, Status::NoData, QString(), detail}; }

    // --- Checks --------------------------------------------------------------

    Check charging() const
    {
        const SensorSeries *voltage = series(kModuleVoltage);
        if (!voltage || !voltage->hasValue())
            voltage = series(kAdapterVoltage);
        const QString title = tr("Battery and charging");
        if (!voltage || !voltage->hasValue())
            return noData(title, tr("Needs the battery voltage (control module voltage or the adapter's reading)."));

        std::vector<double> engineRunning;
        std::vector<double> engineOff;
        for (const auto &sample : recent(voltage, 30)) {
            const auto rpm = at(m_rpm, sample.t, 5);
            if (rpm && *rpm > 400)
                engineRunning.push_back(sample.v);
            else if (rpm)
                engineOff.push_back(sample.v);
        }
        if (engineRunning.size() >= 3) {
            const double v = median(engineRunning);
            const QString value = num(v, 2) + QStringLiteral(" V");
            if (v < 13.0)
                return {title, Status::Bad, value, tr("The alternator does not seem to charge: a running engine should give 13.5–14.8 V. Check the alternator, its belt and the battery terminals.")};
            if (v < 13.5)
                return {title, Status::Warn, value, tr("Charging voltage is on the low side (13.5–14.8 V is normal). Some newer cars lower it on purpose while cruising.")};
            if (v <= 14.9)
                return {title, Status::Ok, value, tr("Normal charging voltage with the engine running (13.5–14.8 V).")};
            if (v <= 15.3)
                return {title, Status::Warn, value, tr("Charging voltage is a little high. Watch it; it can be normal on a cold battery.")};
            return {title, Status::Bad, value, tr("Overcharging: the voltage regulator may be faulty, which damages the battery.")};
        }
        if (engineOff.size() >= 3) {
            const double v = median(engineOff);
            const QString value = num(v, 2) + QStringLiteral(" V");
            if (v >= 12.4)
                return {title, Status::Ok, value, tr("The battery is charged (12.4 V or more with the engine off).")};
            if (v >= 12.0)
                return {title, Status::Warn, value, tr("The battery is partly discharged (12.0–12.4 V with the engine off).")};
            return {title, Status::Bad, value, tr("The battery is low (under 12.0 V with the engine off). Charge it and have it tested.")};
        }
        return {title, Status::Info, num(voltage->value(), 2) + QStringLiteral(" V"),
                tr("Needs engine speed for a verdict: it tells whether the engine is running.")};
    }

    // Returns the average total trim of the bank when there is enough data.
    std::optional<double> fuelTrim(QList<Check> &checks, int bank, int shortPid, int longPid) const
    {
        const SensorSeries *shortTerm = series(shortPid);
        const SensorSeries *longTerm = series(longPid);
        const QString title = tr("Fuel trim, bank %1").arg(bank);
        if (!shortTerm && !longTerm) {
            if (bank == 1)
                checks << noData(title, tr("The car does not report its fuel trims."));
            return std::nullopt;
        }

        // Total trim = short + long term, taken at the more frequent one's samples.
        const SensorSeries *base = shortTerm ? shortTerm : longTerm;
        const SensorSeries *other = shortTerm ? longTerm : nullptr;
        std::vector<double> total;
        std::vector<double> atIdle;
        std::vector<double> atCruise;
        std::vector<double> longValues;
        for (const auto &sample : recent(base, 180)) {
            if (!warmRunning(sample.t))
                continue;
            double trim = sample.v;
            if (other) {
                const auto lt = at(other, sample.t, 10);
                if (!lt)
                    continue;
                trim += *lt;
                longValues.push_back(*lt);
            }
            total.push_back(trim);
            const auto rpm = at(m_rpm, sample.t);
            if (idling(sample.t))
                atIdle.push_back(trim);
            else if (rpm && *rpm >= 1800 && *rpm <= 3500)
                atCruise.push_back(trim);
        }
        if (total.size() < 5) {
            checks << Check{title, Status::Info, QString(), tr("Waiting for a warm, running engine.")};
            return std::nullopt;
        }

        const double avg = mean(total);
        QString value = signedNum(avg, 1) + QStringLiteral(" %");
        if (!longValues.empty())
            value += tr(" (long term %1)").arg(signedNum(mean(longValues), 1));

        Status status = Status::Ok;
        QString detail;
        if (std::abs(avg) <= 8) {
            detail = tr("Within ±8 %: the engine gets the mixture it expects.");
        } else {
            status = std::abs(avg) <= 15 ? Status::Warn : Status::Bad;
            detail = avg > 0
                ? tr("Running lean: the engine computer adds fuel. Common causes: an air leak after the air flow sensor (vacuum hose, intake gasket, PCV), a dirty MAF sensor, low fuel pressure or clogged injectors. A fault code usually comes only at about 25 %.")
                : tr("Running rich: the engine computer takes fuel away. Common causes: a leaking injector, high fuel pressure, a purge valve stuck open, or a faulty O2 sensor.");
        }
        if (atIdle.size() >= 3 && atCruise.size() >= 3) {
            const double idle = mean(atIdle);
            const double cruise = mean(atCruise);
            if (idle - cruise >= 8 && idle > 8)
                detail += QLatin1Char(' ') + tr("It is leaner at idle (%1 %) than at 1800–3500 rpm (%2 %), the typical sign of a vacuum leak.").arg(signedNum(idle, 1), signedNum(cruise, 1));
            else if (cruise - idle >= 8 && cruise > 8)
                detail += QLatin1Char(' ') + tr("It is leaner at 1800–3500 rpm (%1 %) than at idle (%2 %), typical of a weak fuel pump, a clogged fuel filter or a dirty MAF sensor.").arg(signedNum(cruise, 1), signedNum(idle, 1));
        }
        checks << Check{title, status, value, detail};
        return avg;
    }

    static Check bankBalance(double bank1, double bank2)
    {
        const QString title = tr("Fuel trim, bank 1 against bank 2");
        const double diff = bank1 - bank2;
        const QString value = signedNum(diff, 1) + QStringLiteral(" %");
        if (std::abs(diff) <= 10)
            return {title, Status::Ok, value, tr("Both sides of the engine need about the same correction.")};
        return {title, Status::Warn, value, tr("One side of the engine runs leaner than the other: look for a leak or an injector problem on that side (intake gasket, injector, O2 sensor).")};
    }

    Check coolant() const
    {
        const QString title = tr("Engine temperature");
        if (!m_coolant || !m_coolant->hasValue())
            return noData(title, tr("The car does not report the coolant temperature."));
        const double latest = m_coolant->value();
        const QString value = num(latest, 0) + QStringLiteral(" °C");
        if (latest < -35 || latest > 140)
            return {title, Status::Bad, value, tr("Implausible reading: the coolant temperature sensor or its wiring may be faulty.")};

        double highest = -100;
        for (const auto &sample : m_coolant->samples())
            highest = std::max(highest, sample.v);
        double recentHighest = -100;
        for (const auto &sample : recent(m_coolant, 300))
            recentHighest = std::max(recentHighest, sample.v);

        if (highest >= 80) {
            if (recentHighest > 110)
                return {title, Status::Bad, value, tr("Overheating (over 110 °C): let the engine cool down, then check the coolant level, the radiator fan, the water pump and the thermostat.")};
            if (recentHighest > 104)
                return {title, Status::Warn, value, tr("Running hot (over 104 °C). About 85–100 °C is normal; check the coolant level and the radiator fan.")};
            return {title, Status::Ok, value, tr("Reaches normal operating temperature.")};
        }
        const double minutes = runningSeconds() / 60;
        if (minutes >= 15) {
            if (highest < 70)
                return {title, Status::Bad, value, tr("Still under 70 °C after %1 minutes of running: the thermostat is probably stuck open (higher fuel use, weak heater).").arg(num(minutes, 0))};
            return {title, Status::Warn, value, tr("Slow to warm up (under 80 °C after %1 minutes). In cold weather this can be normal; otherwise suspect the thermostat.").arg(num(minutes, 0))};
        }
        return {title, Status::Info, value, tr("Warming up (%1 minutes of running so far).").arg(num(minutes, 0))};
    }

    Check coldStart() const
    {
        const QString title = tr("Temperature sensors on a cold start");
        const SensorSeries *intake = series(kIntakeAir);
        if (!m_coolant || !intake || m_coolant->samples().empty() || intake->samples().empty())
            return noData(title, tr("Needs the coolant and intake air temperatures."));
        const auto &first = m_coolant->samples().front();
        if (first.v > 45)
            return {title, Status::Info, QString(), tr("Checked only when the session starts with a cold engine.")};
        const auto air = at(intake, first.t + 60, 60);
        if (!air)
            return {title, Status::Info, QString(), tr("Waiting for the intake air temperature.")};
        const double diff = first.v - *air;
        const QString value = tr("coolant %1 °C, intake air %2 °C").arg(num(first.v, 0), num(*air, 0));
        if (std::abs(diff) <= 8)
            return {title, Status::Ok, value, tr("On a cold engine both sensors read about the same, as they should.")};
        return {title, Status::Warn, value, tr("On a cold engine these should be close. If the car stood for several hours before, one of the two sensors reads wrong.")};
    }

    Check idleSpeed() const
    {
        const QString title = tr("Idle");
        if (!m_rpm)
            return noData(title, tr("The car does not report engine speed."));
        const std::vector<double> rpm = valuesWhen(m_rpm, 120, [this](double t) { return idling(t) && warmRunning(t); });
        if (rpm.size() < 8)
            return {title, Status::Info, QString(), tr("Let the warm engine idle for a minute to check this.")};
        const double avg = mean(rpm);
        const double spread = stddev(rpm);
        const QString value = tr("%1 rpm ± %2").arg(num(avg, 0), num(spread, 0));
        if (spread > 60)
            return {title, Status::Bad, value, tr("Unsteady idle: a misfire, a vacuum leak, or a dirty throttle body or idle valve. The A/C compressor switching on and off also shakes it.")};
        if (spread > 30)
            return {title, Status::Warn, value, tr("Idle speed varies more than usual: an early sign of a misfire, a vacuum leak or a dirty throttle body.")};
        if (avg > 1100)
            return {title, Status::Warn, value, tr("High idle for a warm engine: an air leak or the idle control.")};
        if (avg < 550)
            return {title, Status::Warn, value, tr("Low idle speed: the engine may stall under load.")};
        return {title, Status::Ok, value, tr("Steady idle.")};
    }

    Check idleAirflow() const
    {
        const QString title = tr("Air flow at idle");
        const SensorSeries *maf = series(kMaf);
        if (!maf)
            return noData(title, tr("The car has no mass air flow sensor reading."));
        const std::vector<double> flow = valuesWhen(maf, 120, [this](double t) { return idling(t) && warmRunning(t); });
        if (flow.size() < 5)
            return {title, Status::Info, QString(), tr("Let the warm engine idle for a minute to check this.")};
        const double avg = mean(flow);
        const QString value = num(avg, 1) + QStringLiteral(" g/s");
        if (avg < 1.5)
            return {title, Status::Warn, value, tr("Very low air flow at idle: a dirty MAF sensor, or air entering after it.")};
        if (avg > 10)
            return {title, Status::Warn, value, tr("High air flow at idle: a large engine, a load such as the A/C, or a sticking idle control.")};
        return {title, Status::Ok, value, tr("A usual amount for a warm idling engine (about 2–7 g/s for 1–4 litres).")};
    }

    Check idleLoad() const
    {
        const QString title = tr("Engine load at idle");
        const SensorSeries *load = series(kLoad);
        if (!load)
            return noData(title, tr("The car does not report engine load."));
        const std::vector<double> values = valuesWhen(load, 120, [this](double t) { return idling(t) && warmRunning(t); });
        if (values.size() < 5)
            return {title, Status::Info, QString(), tr("Let the warm engine idle for a minute to check this.")};
        const double avg = mean(values);
        const QString value = num(avg, 0) + QStringLiteral(" %");
        if (avg > 45)
            return {title, Status::Warn, value, tr("High load at idle: the A/C, a big electrical load, a dragging accessory, or an air leak.")};
        return {title, Status::Ok, value, tr("Normal load for idling (about 15–40 %).")};
    }

    void oxygenSensor(QList<Check> &checks, int bank, int pid) const
    {
        const SensorSeries *o2 = series(pid);
        const QString title = tr("O2 sensor, bank %1 before the catalyst").arg(bank);
        if (!o2) {
            if (bank == 1)
                checks << noData(title, tr("No narrow-band O2 sensor reading (wide-band sensors are not checked)."));
            return;
        }
        const std::vector<double> volts = valuesWhen(o2, 60, [this](double t) {
            const auto rpm = at(m_rpm, t);
            return warmRunning(t) && rpm && *rpm > 1000;
        });
        if (volts.size() < 10) {
            checks << Check{title, Status::Info, QString(), tr("Drive with a warm engine above 1000 rpm for a minute to check this.")};
            return;
        }
        const auto [lo, hi] = std::minmax_element(volts.begin(), volts.end());
        const QString value = tr("%1–%2 V").arg(num(*lo, 2), num(*hi, 2));
        if (*hi - *lo >= 0.5 && *lo < 0.3 && *hi > 0.6)
            checks << Check{title, Status::Ok, value, tr("Switches between lean and rich (about 0.1–0.9 V) as it should.")};
        else if (*hi < 0.35)
            checks << Check{title, Status::Warn, value, tr("Stays lean (under 0.35 V): an air leak, low fuel pressure, or a worn sensor.")};
        else if (*lo > 0.55)
            checks << Check{title, Status::Warn, value, tr("Stays rich (over 0.55 V): too much fuel, or a contaminated sensor.")};
        else
            checks << Check{title, Status::Warn, value, tr("Hardly switches: the sensor may be slow or worn. Replacing a lazy sensor often lowers fuel use.")};
    }

    void catalyst(QList<Check> &checks, int bank, int upstreamPid, int downstreamPid) const
    {
        const SensorSeries *up = series(upstreamPid);
        const SensorSeries *down = series(downstreamPid);
        const QString title = tr("Catalyst, bank %1").arg(bank);
        if (!up || !down) {
            if (bank == 1)
                checks << noData(title, tr("Needs the O2 sensors before and after the catalyst."));
            return;
        }
        const auto when = [this](double t) {
            const auto rpm = at(m_rpm, t);
            return warmRunning(t) && rpm && *rpm > 1000;
        };
        const std::vector<double> upstream = valuesWhen(up, 120, when);
        const std::vector<double> downstream = valuesWhen(down, 120, when);
        if (upstream.size() < 10 || downstream.size() < 10) {
            checks << Check{title, Status::Info, QString(), tr("Drive with a warm engine above 1000 rpm for a couple of minutes to check this.")};
            return;
        }
        const auto [upLo, upHi] = std::minmax_element(upstream.begin(), upstream.end());
        const auto [downLo, downHi] = std::minmax_element(downstream.begin(), downstream.end());
        const double upRange = *upHi - *upLo;
        const double downRange = *downHi - *downLo;
        const QString value = tr("before %1–%2 V, after %3–%4 V")
                                  .arg(num(*upLo, 2), num(*upHi, 2), num(*downLo, 2), num(*downHi, 2));
        if (upRange < 0.4) {
            checks << Check{title, Status::Info, value, tr("Waiting for the sensor before the catalyst to switch (steady driving).")};
            return;
        }
        const double ratio = downRange / upRange;
        if (ratio < 0.4)
            checks << Check{title, Status::Ok, value, tr("The sensor after the catalyst stays steady: the catalyst still stores oxygen.")};
        else if (ratio < 0.7)
            checks << Check{title, Status::Warn, value, tr("The sensor after the catalyst partly follows the one before it: the catalyst may be ageing.")};
        else
            checks << Check{title, Status::Bad, value, tr("The sensor after the catalyst mirrors the one before it: the catalyst hardly works any more (a P0420/P0430 code is likely to follow).")};
    }

    Check knock() const
    {
        const QString title = tr("Ignition timing under load");
        const SensorSeries *timing = series(kTiming);
        const SensorSeries *load = series(kLoad);
        if (!timing || !load || !m_rpm)
            return noData(title, tr("Needs timing advance, engine load and engine speed."));
        const std::vector<double> advance = valuesWhen(timing, 300, [this, load](double t) {
            const auto l = at(load, t);
            const auto rpm = at(m_rpm, t);
            return l && *l >= 60 && rpm && *rpm >= 1800 && *rpm <= 5000;
        });
        if (advance.size() < 5)
            return {title, Status::Info, QString(), tr("Accelerate firmly a few times to check this.")};
        const double avg = mean(advance);
        const QString value = num(avg, 1) + QStringLiteral("°");
        if (avg < 3)
            return {title, Status::Warn, value, tr("Little ignition advance under load: the engine computer may be pulling timing because of knock. Try higher-octane fuel; carbon build-up or a knock sensor fault are other causes. Turbo engines run less advance, so compare with your own healthy recordings.")};
        return {title, Status::Ok, value, tr("Ignition advance under load looks normal.")};
    }

    Check intakeHeat() const
    {
        const QString title = tr("Intake air temperature");
        const SensorSeries *intake = series(kIntakeAir);
        const SensorSeries *ambient = series(kAmbient);
        if (!intake || !ambient)
            return noData(title, tr("Needs the intake air and outside temperatures."));
        std::vector<double> rise;
        for (const auto &sample : recent(intake, 300)) {
            const auto speed = at(m_speed, sample.t);
            const auto outside = at(ambient, sample.t, 30);
            if (speed && *speed > 50 && outside)
                rise.push_back(sample.v - *outside);
        }
        if (rise.size() < 5)
            return {title, Status::Info, QString(), tr("Drive above 50 km/h for a few minutes to check this.")};
        const double avg = mean(rise);
        const QString value = tr("%1 °C above outside").arg(signedNum(avg, 0));
        if (avg > 25)
            return {title, Status::Warn, value, tr("The engine breathes hot air while driving: check the intercooler (turbo engines) and the air intake routing.")};
        return {title, Status::Ok, value, tr("Intake air stays close to the outside temperature while driving.")};
    }

    const Lookup &m_lookup;
    double m_now;
    const SensorSeries *m_rpm;
    const SensorSeries *m_speed;
    const SensorSeries *m_coolant;
};

} // namespace

QList<Check> evaluate(const Lookup &series, double now)
{
    return Evaluator(series, now).run();
}

QString statusKey(Status status)
{
    switch (status) {
    case Status::Ok: return QStringLiteral("ok");
    case Status::Warn: return QStringLiteral("warn");
    case Status::Bad: return QStringLiteral("bad");
    case Status::Info: return QStringLiteral("info");
    case Status::NoData: return QStringLiteral("nodata");
    }
    return QString();
}

} // namespace health
