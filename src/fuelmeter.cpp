#include "fuelmeter.h"

#include <QElapsedTimer>
#include <QLocale>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kSpeed = 0x0D;
constexpr int kMap = 0x0B;
constexpr int kRpm = 0x0C;
constexpr int kIntakeTemp = 0x0F;
constexpr int kMaf = 0x10;
constexpr int kLambda = 0x44;
constexpr int kFuelRate = 0x5E;

constexpr double kWindowSeconds = 6;    // "now" averages over this much driving
constexpr double kStaleSeconds = 5;     // no "now" without a fuel reading this recent
constexpr double kInputAge = 3;         // engine readings combined into one estimate
constexpr double kIntakeTempAge = 60;   // intake temperature changes slowly
constexpr double kMovingKmh = 5;        // below this, consumption is shown per hour
constexpr int kMaxHistory = 20;

QString number(double value, int decimals)
{
    return QLocale::c().toString(value, 'f', decimals);
}

QString formatKm(double km)
{
    return QStringLiteral("%1 km").arg(number(km, km < 10 ? 2 : km < 1000 ? 1 : 0));
}

QString formatLiters(double liters)
{
    return QStringLiteral("%1 L").arg(number(liters, liters < 10 ? 2 : liters < 100 ? 1 : 0));
}

QString formatPer100(double value)
{
    return value < 0 ? QStringLiteral("—") : number(value, 1);
}

QString formatDuration(double seconds)
{
    const auto total = qint64(std::max(0.0, seconds));
    const qint64 h = total / 3600;
    const qint64 m = total / 60 % 60;
    const qint64 s = total % 60;
    if (h >= 100)
        return QObject::tr("%1 h").arg(h);
    return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatPeriod(const QDateTime &start, const QDateTime &end)
{
    const QLocale locale;
    const QString from = locale.toString(start, QLocale::ShortFormat);
    if (!end.isValid())
        return from;
    const QString to = end.date() == start.date() ? locale.toString(end.time(), QLocale::ShortFormat)
                                                  : locale.toString(end, QLocale::ShortFormat);
    return QStringLiteral("%1 – %2").arg(from, to);
}

} // namespace

FuelMeter::FuelMeter(const QElapsedTimer *clock, QObject *parent)
    : QObject(parent)
    , m_clock(clock)
{
    QSettings s;
    m_kind = fuel::Kind(std::clamp(s.value(QStringLiteral("fuel/type"), 0).toInt(), 0, 2));
    m_engineLiters = std::clamp(s.value(QStringLiteral("fuel/engineLiters"), 1.6).toDouble(), 0.5, 10.0);
    m_correction = std::clamp(s.value(QStringLiteral("fuel/correction"), 100).toInt(), 50, 150);
    m_total = loadTotals(s, QStringLiteral("fuel/total"));
    m_totalSince = s.value(QStringLiteral("fuel/total/since")).toDateTime();
    if (!m_totalSince.isValid())
        m_totalSince = QDateTime::currentDateTime();
    m_tripRunning = s.value(QStringLiteral("fuel/trip/running"), false).toBool();
    m_trip = loadTotals(s, QStringLiteral("fuel/trip"));
    m_tripStart = s.value(QStringLiteral("fuel/trip/started")).toDateTime();
    m_tripEnd = s.value(QStringLiteral("fuel/trip/stopped")).toDateTime();
    if (m_tripRunning && !m_tripStart.isValid())
        m_tripStart = QDateTime::currentDateTime();
    m_history = s.value(QStringLiteral("fuel/history")).toList();

    m_notifyTimer.setSingleShot(true);
    m_notifyTimer.setInterval(400);
    connect(&m_notifyTimer, &QTimer::timeout, this, &FuelMeter::changed);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(15000);
    connect(&m_saveTimer, &QTimer::timeout, this, &FuelMeter::save);
    m_tickTimer.setInterval(1000);
    connect(&m_tickTimer, &QTimer::timeout, this, &FuelMeter::changed);
}

FuelMeter::~FuelMeter()
{
    if (m_dirty)
        save();
}

// --- Settings ------------------------------------------------------------------

void FuelMeter::setFuelType(int type)
{
    type = std::clamp(type, 0, 2);
    if (type == int(m_kind))
        return;
    m_kind = fuel::Kind(type);
    QSettings().setValue(QStringLiteral("fuel/type"), type);
    emit settingsChanged();
    chooseMethod();
    touch();
}

void FuelMeter::setEngineLiters(double liters)
{
    liters = std::round(std::clamp(liters, 0.5, 10.0) * 10.0) / 10.0;
    if (qFuzzyCompare(liters, m_engineLiters))
        return;
    m_engineLiters = liters;
    QSettings().setValue(QStringLiteral("fuel/engineLiters"), liters);
    emit settingsChanged();
}

void FuelMeter::setCorrection(int percent)
{
    percent = std::clamp(percent, 50, 150);
    if (percent == m_correction)
        return;
    m_correction = percent;
    QSettings().setValue(QStringLiteral("fuel/correction"), percent);
    emit settingsChanged();
    touch();
}

void FuelMeter::setOnScreen(bool on)
{
    if (m_onScreen == on)
        return;
    m_onScreen = on;
    updateTick();
    emit onScreenChanged();
}

// --- Session -------------------------------------------------------------------

void FuelMeter::startSession(const QSet<int> &pids, bool newDrive)
{
    m_connected = true;
    m_pids = pids;
    m_speedIntegrator.reset();
    m_rateIntegrator.reset();
    m_speed = m_rate = m_map = m_rpm = m_intakeTemp = m_lambda = Reading{};
    m_window.clear();
    if (newDrive)
        m_drive = {};
    chooseMethod();
    emit methodChanged(); // the text changes from "connect to the car" even if the method does not
    touch();
}

void FuelMeter::setAvailablePids(const QSet<int> &pids)
{
    m_pids = pids;
    chooseMethod();
}

void FuelMeter::endSession()
{
    if (!m_connected)
        return;
    m_connected = false;
    m_pids.clear();
    m_window.clear();
    chooseMethod();
    emit methodChanged();
    save();
    touch();
}

void FuelMeter::chooseMethod()
{
    const fuel::Method method = m_connected ? fuel::chooseMethod(m_pids, m_kind) : fuel::Method::None;
    const QList<int> inputs = m_connected ? fuel::inputs(method, m_pids) : QList<int>();
    if (method != m_method) {
        m_method = method;
        m_rateIntegrator.reset();
        m_rate = Reading{};
        emit methodChanged();
    }
    if (inputs != m_inputs) {
        m_inputs = inputs;
        emit inputsChanged();
    }
}

void FuelMeter::addSample(int pid, double t, double value)
{
    if (!m_connected)
        return;
    switch (pid) {
    case kSpeed: {
        m_speed = {t, value};
        const fuel::Integrator::Step step = m_speedIntegrator.add(t, value);
        add(step.area / 3600.0, 0, step.seconds);
        snapshot(t);
        break;
    }
    case kFuelRate:
        if (m_method == fuel::Method::FuelRate)
            addRate(t, value);
        break;
    case kMaf:
        if (m_method == fuel::Method::Maf)
            addRate(t, fuel::litersPerHour(value, m_kind, m_lambda.fresh(t, kInputAge) ? m_lambda.v : 1.0));
        break;
    case kLambda:
        m_lambda = {t, value};
        break;
    case kMap:
        m_map = {t, value};
        addSpeedDensity(t);
        break;
    case kRpm:
        m_rpm = {t, value};
        addSpeedDensity(t);
        break;
    case kIntakeTemp:
        m_intakeTemp = {t, value};
        break;
    default:
        break;
    }
}

void FuelMeter::addSpeedDensity(double t)
{
    if (m_method != fuel::Method::SpeedDensity || !m_map.fresh(t, kInputAge) || !m_rpm.fresh(t, kInputAge))
        return;
    const double intake = m_intakeTemp.fresh(t, kIntakeTempAge) ? m_intakeTemp.v : 25.0;
    const double air = fuel::speedDensityAir(m_map.v, m_rpm.v, intake, m_engineLiters);
    addRate(t, fuel::litersPerHour(air, m_kind));
}

void FuelMeter::addRate(double t, double litersPerHour)
{
    m_rate = {t, std::max(0.0, litersPerHour)};
    const fuel::Integrator::Step step = m_rateIntegrator.add(t, m_rate.v);
    add(0, step.area / 3600.0, 0);
    snapshot(t);
}

void FuelMeter::add(double km, double liters, double seconds)
{
    if (km <= 0 && liters <= 0 && seconds <= 0) {
        touch();
        return;
    }
    for (fuel::Totals *totals : {&m_drive, &m_total}) {
        totals->km += km;
        totals->liters += liters;
        totals->seconds += seconds;
    }
    if (m_tripRunning) {
        m_trip.km += km;
        m_trip.liters += liters;
        m_trip.seconds += seconds;
    }
    markDirty();
    touch();
}

void FuelMeter::snapshot(double t)
{
    m_window.push_back({t, m_drive.km, m_drive.liters});
    while (m_window.size() > 2 && m_window.front().t < t - kWindowSeconds)
        m_window.pop_front();
}

double FuelMeter::clockNow() const
{
    if (m_clock && m_clock->isValid())
        return m_clock->elapsed() / 1000.0;
    return std::max(m_rate.t, m_speed.t);
}

FuelMeter::Now FuelMeter::now() const
{
    Now result;
    if (!m_connected || m_method == fuel::Method::None || !m_rate.fresh(clockNow(), kStaleSeconds))
        return result;
    result.valid = true;
    double km = 0;
    double liters = 0;
    double seconds = 0;
    if (m_window.size() >= 2) {
        km = m_window.back().km - m_window.front().km;
        liters = m_window.back().liters - m_window.front().liters;
        seconds = m_window.back().t - m_window.front().t;
    }
    if (seconds < 1) {
        // Just connected: the latest readings.
        result.kmh = m_speed.fresh(m_rate.t, kInputAge) ? m_speed.v : 0;
        result.litersPerHour = m_rate.v * factor();
    } else {
        result.kmh = km / seconds * 3600.0;
        result.litersPerHour = liters / seconds * 3600.0 * factor();
    }
    result.perDistance = result.kmh >= kMovingKmh;
    result.value = result.perDistance ? result.litersPerHour / result.kmh * 100.0 : result.litersPerHour;
    return result;
}

// --- Measurement, total and history ------------------------------------------

void FuelMeter::startTrip()
{
    m_tripRunning = true;
    m_trip = {};
    m_tripStart = QDateTime::currentDateTime();
    m_tripEnd = QDateTime();
    updateTick();
    save();
    touch();
}

void FuelMeter::stopTrip()
{
    if (!m_tripRunning)
        return;
    m_tripRunning = false;
    m_tripEnd = QDateTime::currentDateTime();
    m_history.prepend(QVariantMap{
        {QStringLiteral("start"), m_tripStart},
        {QStringLiteral("end"), m_tripEnd},
        {QStringLiteral("km"), m_trip.km},
        {QStringLiteral("liters"), m_trip.liters},
        {QStringLiteral("seconds"), m_trip.seconds},
    });
    while (m_history.size() > kMaxHistory)
        m_history.removeLast();
    updateTick();
    save();
    touch();
}

void FuelMeter::resetTotal()
{
    m_total = {};
    m_totalSince = QDateTime::currentDateTime();
    save();
    touch();
}

void FuelMeter::clearHistory()
{
    // The finished measurement shown in the card stays.
    while (m_history.size() > (m_tripRunning ? 0 : 1))
        m_history.removeLast();
    save();
    touch();
}

void FuelMeter::save()
{
    m_saveTimer.stop();
    QSettings s;
    saveTotals(s, QStringLiteral("fuel/total"), m_total);
    s.setValue(QStringLiteral("fuel/total/since"), m_totalSince);
    s.setValue(QStringLiteral("fuel/trip/running"), m_tripRunning);
    saveTotals(s, QStringLiteral("fuel/trip"), m_trip);
    s.setValue(QStringLiteral("fuel/trip/started"), m_tripStart);
    s.setValue(QStringLiteral("fuel/trip/stopped"), m_tripEnd);
    s.setValue(QStringLiteral("fuel/history"), m_history);
    m_dirty = false;
}

fuel::Totals FuelMeter::loadTotals(const QSettings &settings, const QString &group)
{
    fuel::Totals totals;
    totals.km = std::max(0.0, settings.value(group + QStringLiteral("/km"), 0.0).toDouble());
    totals.liters = std::max(0.0, settings.value(group + QStringLiteral("/liters"), 0.0).toDouble());
    totals.seconds = std::max(0.0, settings.value(group + QStringLiteral("/seconds"), 0.0).toDouble());
    return totals;
}

void FuelMeter::saveTotals(QSettings &settings, const QString &group, const fuel::Totals &totals)
{
    settings.setValue(group + QStringLiteral("/km"), totals.km);
    settings.setValue(group + QStringLiteral("/liters"), totals.liters);
    settings.setValue(group + QStringLiteral("/seconds"), totals.seconds);
}

void FuelMeter::markDirty()
{
    m_dirty = true;
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void FuelMeter::touch()
{
    if (!m_notifyTimer.isActive())
        m_notifyTimer.start();
}

void FuelMeter::updateTick()
{
    if (m_tripRunning && m_onScreen)
        m_tickTimer.start();
    else
        m_tickTimer.stop();
}

// --- What QML shows ------------------------------------------------------------

QString FuelMeter::methodKey() const
{
    if (!m_connected)
        return QString();
    switch (m_method) {
    case fuel::Method::None: return QStringLiteral("none");
    case fuel::Method::FuelRate: return QStringLiteral("rate");
    case fuel::Method::Maf: return QStringLiteral("maf");
    case fuel::Method::SpeedDensity: return QStringLiteral("map");
    }
    return QString();
}

QString FuelMeter::methodText() const
{
    if (!m_connected)
        return tr("Connect to the car to measure. The measurement and the total carry on from where they were.");
    switch (m_method) {
    case fuel::Method::FuelRate:
        return tr("Fuel rate reported by the engine (PID 5E).");
    case fuel::Method::Maf:
        return m_inputs.contains(kLambda)
                   ? tr("Estimated from the air flow sensor (MAF) and the commanded air-fuel ratio. Compare with a fill-up and set the correction below.")
                   : tr("Estimated from the air flow sensor (MAF). Compare with a fill-up and set the correction below.");
    case fuel::Method::SpeedDensity:
        return tr("Estimated from intake pressure, engine speed and the engine size below, as the car has no air flow sensor. Set the engine size, then compare with a fill-up and set the correction.");
    case fuel::Method::None:
        break;
    }
    if (!m_pids.contains(kSpeed))
        return tr("The car does not report its speed, so consumption per distance cannot be measured.");
    if (m_kind == fuel::Kind::Diesel)
        return tr("The car does not report its fuel rate, and a diesel's fuel use cannot be estimated from the air it takes in.");
    return tr("The car reports neither its fuel rate, nor air flow, nor intake pressure.");
}

QString FuelMeter::nowValue() const
{
    const Now n = now();
    return n.valid ? number(n.value, 1) : QStringLiteral("—");
}

QString FuelMeter::nowUnit() const
{
    const Now n = now();
    return n.valid && !n.perDistance ? tr("L/h") : tr("L/100 km");
}

QString FuelMeter::nowDetail() const
{
    const Now n = now();
    if (!n.valid)
        return QString();
    return tr("%1 km/h · %2 L/h").arg(number(n.kmh, 0), number(n.litersPerHour, 1));
}

QVariantMap FuelMeter::totalsMap(const fuel::Totals &totals, double seconds) const
{
    return {
        {QStringLiteral("distance"), formatKm(totals.km)},
        {QStringLiteral("fuel"), formatLiters(totals.liters * factor())},
        {QStringLiteral("average"), formatPer100(totals.per100km(factor()))},
        {QStringLiteral("time"), formatDuration(seconds)},
    };
}

QVariantMap FuelMeter::trip() const
{
    const QDateTime end = m_tripRunning ? QDateTime::currentDateTime() : m_tripEnd;
    const double seconds = m_tripStart.isValid() && end.isValid() ? m_tripStart.secsTo(end) : 0;
    QVariantMap map = totalsMap(m_trip, seconds);
    map.insert(QStringLiteral("has"), m_tripRunning || m_tripStart.isValid());
    map.insert(QStringLiteral("period"),
               !m_tripStart.isValid() ? QString()
               : m_tripRunning        ? tr("since %1").arg(QLocale().toString(m_tripStart, QLocale::ShortFormat))
                                      : formatPeriod(m_tripStart, m_tripEnd));
    return map;
}

QVariantMap FuelMeter::drive() const
{
    return totalsMap(m_drive, m_drive.seconds);
}

QVariantMap FuelMeter::total() const
{
    QVariantMap map = totalsMap(m_total, m_total.seconds);
    map.insert(QStringLiteral("since"), QLocale().toString(m_totalSince.date(), QLocale::ShortFormat));
    return map;
}

QVariantList FuelMeter::history() const
{
    QVariantList list;
    for (const QVariant &item : m_history) {
        const QVariantMap raw = item.toMap();
        fuel::Totals totals;
        totals.km = raw.value(QStringLiteral("km")).toDouble();
        totals.liters = raw.value(QStringLiteral("liters")).toDouble();
        const QDateTime start = raw.value(QStringLiteral("start")).toDateTime();
        const QDateTime end = raw.value(QStringLiteral("end")).toDateTime();
        QVariantMap map = totalsMap(totals, start.isValid() && end.isValid() ? start.secsTo(end) : 0);
        map.insert(QStringLiteral("period"), start.isValid() ? formatPeriod(start, end) : QString());
        list << map;
    }
    return list;
}

QStringList FuelMeter::summary() const
{
    QStringList lines;
    const Now n = now();
    if (n.valid)
        lines << tr("Now %1 %2 · %3 km/h").arg(number(n.value, 1), nowUnit(), number(n.kmh, 0));
    const auto line = [this](const QString &label, const fuel::Totals &totals) {
        const double average = totals.per100km(factor());
        return average < 0 || m_method == fuel::Method::None
                   ? tr("%1: %2").arg(label, formatKm(totals.km))
                   : tr("%1: %2 at %3 L/100 km").arg(label, formatKm(totals.km), formatPer100(average));
    };
    if (m_tripRunning)
        lines << line(tr("Measurement"), m_trip);
    lines << line(tr("This drive"), m_drive);
    return lines;
}
