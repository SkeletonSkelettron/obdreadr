#include "recording.h"

#include "obd/pids.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace recording {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Recording", text);
}

QString num(double v, int decimals)
{
    return QLocale::c().toString(v, 'f', decimals);
}

QString duration(double seconds)
{
    const auto total = qint64(std::max(0.0, seconds));
    return QStringLiteral("%1:%2:%3")
        .arg(total / 3600)
        .arg(total / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'));
}

// One CSV line, with "quoted, fields" and "" for a quote inside them.
QStringList splitCsv(const QString &line)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    field += c;
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(',')) {
            fields << field;
            field.clear();
        } else {
            field += c;
        }
    }
    fields << field;
    return fields;
}

const Column *column(const Data &data, int pid)
{
    for (const Column &c : data.columns) {
        if (c.pid == pid && !c.points.empty())
            return &c;
    }
    return nullptr;
}

// The last value at or before t, if at most maxAge seconds old.
std::optional<double> at(const Column *c, double t, double maxAge)
{
    if (!c)
        return std::nullopt;
    auto it = std::upper_bound(c->points.begin(), c->points.end(), t,
                               [](double time, const Point &p) { return time < p.t; });
    if (it == c->points.begin())
        return std::nullopt;
    --it;
    if (t - it->t > maxAge)
        return std::nullopt;
    return it->v;
}

double mean(const std::vector<double> &v)
{
    double sum = 0;
    for (double x : v)
        sum += x;
    return v.empty() ? 0 : sum / double(v.size());
}

} // namespace

std::optional<Data> parse(const QByteArray &csv, QString *error)
{
    const QList<QByteArray> lines = csv.split('\n');
    if (lines.isEmpty() || !lines.first().startsWith("time_s,timestamp")) {
        if (error)
            *error = tr("This is not a recording of this app.");
        return std::nullopt;
    }

    Data data;
    static const QRegularExpression kNameUnit(QStringLiteral("^(.*) \\[(.*)\\]$"));
    const QStringList header = splitCsv(QString::fromUtf8(lines.first().trimmed()));
    for (qsizetype i = 2; i < header.size(); ++i) {
        Column c;
        const QRegularExpressionMatch match = kNameUnit.match(header[i]);
        c.name = match.hasMatch() ? match.captured(1) : header[i];
        c.unit = match.hasMatch() ? match.captured(2) : QString();
        c.pid = obd::pidByName(c.name);
        data.columns << c;
    }

    bool first = true;
    for (qsizetype row = 1; row < lines.size(); ++row) {
        const QByteArray line = lines[row].trimmed();
        if (line.isEmpty())
            continue;
        const QList<QByteArray> fields = line.split(',');
        bool ok = false;
        const double t = fields.first().toDouble(&ok);
        if (!ok || fields.size() < 2)
            continue;
        if (first) {
            first = false;
            data.firstTime = t;
            const QDateTime stamp = QDateTime::fromString(QString::fromLatin1(fields[1]),
                                                          QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
            data.start = stamp.isValid() ? stamp.addMSecs(-qint64(t * 1000)) : QDateTime();
        }
        data.lastTime = t;
        data.rowTimes << t;
        for (qsizetype i = 2; i < fields.size() && i - 2 < data.columns.size(); ++i) {
            if (fields[i].isEmpty())
                continue;
            const double v = fields[i].toDouble(&ok);
            if (ok)
                data.columns[i - 2].points.push_back({t, v});
        }
    }
    if (data.rowTimes.isEmpty()) {
        if (error)
            *error = tr("The recording has no readings.");
        return std::nullopt;
    }
    return data;
}

QList<QPair<QString, QString>> summarize(const Data &data, const FuelSettings &settings)
{
    QList<QPair<QString, QString>> out;
    out << qMakePair(tr("Duration"), duration(data.lastTime - data.firstTime));

    // Gaps: the adapter or the phone stopped for a while.
    int gaps = 0;
    double missing = 0;
    for (qsizetype i = 1; i < data.rowTimes.size(); ++i) {
        const double dt = data.rowTimes[i] - data.rowTimes[i - 1];
        if (dt > 10) {
            ++gaps;
            missing += dt;
        }
    }
    out << qMakePair(tr("Gaps"),
                     gaps == 0 ? tr("none")
                               : QCoreApplication::translate("Recording", "%n gap(s), %1 without data", nullptr, gaps)
                                     .arg(duration(missing)));

    const Column *speed = column(data, 0x0D);
    const Column *rpm = column(data, 0x0C);
    const Column *coolant = column(data, 0x05);

    double km = -1;
    if (speed) {
        fuel::Integrator integrator;
        km = 0;
        for (const Point &p : speed->points)
            km += integrator.add(p.t, p.v).area / 3600.0;
        out << qMakePair(tr("Distance"), num(km, km < 10 ? 2 : 1) + QStringLiteral(" km"));
    }

    // Fuel, the same way as the Fuel tab with the current settings.
    QSet<int> pids;
    for (const Column &c : data.columns) {
        if (!c.points.empty() && c.pid >= 0)
            pids.insert(c.pid);
    }
    const fuel::Method method = fuel::chooseMethod(pids, settings.kind);
    fuel::Integrator rate;
    double liters = 0;
    const auto addRate = [&](double t, double litersPerHour) {
        liters += rate.add(t, std::max(0.0, litersPerHour)).area / 3600.0;
    };
    switch (method) {
    case fuel::Method::FuelRate:
        for (const Point &p : column(data, 0x5E)->points)
            addRate(p.t, p.v);
        break;
    case fuel::Method::Maf: {
        const Column *lambda = column(data, 0x44);
        for (const Point &p : column(data, 0x10)->points)
            addRate(p.t, fuel::litersPerHour(p.v, settings.kind, at(lambda, p.t, 3).value_or(1.0)));
        break;
    }
    case fuel::Method::SpeedDensity: {
        const Column *intake = column(data, 0x0F);
        for (const Point &p : column(data, 0x0B)->points) {
            const auto r = at(rpm, p.t, 3);
            if (!r)
                continue;
            const double air = fuel::speedDensityAir(p.v, *r, at(intake, p.t, 60).value_or(25.0), settings.engineLiters);
            addRate(p.t, fuel::litersPerHour(air, settings.kind));
        }
        break;
    }
    case fuel::Method::None:
        break;
    }
    if (method != fuel::Method::None) {
        liters *= settings.correction;
        out << qMakePair(tr("Fuel"), num(liters, liters < 10 ? 2 : 1) + QStringLiteral(" L"));
        if (km >= 1)
            out << qMakePair(tr("Average"), num(liters / km * 100, 1) + QStringLiteral(" L/100 km"));
        if (settings.price > 0)
            out << qMakePair(tr("Cost"), num(liters * settings.price, 2) + QStringLiteral(" ₾"));
    }

    const auto highest = [](const Column *c) {
        double best = c->points.front().v;
        for (const Point &p : c->points)
            best = std::max(best, p.v);
        return best;
    };
    if (speed)
        out << qMakePair(tr("Top speed"), num(highest(speed), 0) + QStringLiteral(" km/h"));
    if (rpm)
        out << qMakePair(tr("Highest engine speed"), num(highest(rpm), 0) + QStringLiteral(" rpm"));
    if (coolant)
        out << qMakePair(tr("Highest coolant temperature"), num(highest(coolant), 0) + QStringLiteral(" °C"));

    const Column *volts = column(data, 0x42);
    if (!volts)
        volts = column(data, -1);
    if (volts) {
        double lo = volts->points.front().v;
        double hi = lo;
        for (const Point &p : volts->points) {
            lo = std::min(lo, p.v);
            hi = std::max(hi, p.v);
        }
        out << qMakePair(tr("Voltage"), QStringLiteral("%1–%2 V").arg(num(lo, 1), num(hi, 1)));
    }

    // Warm idle and fuel trim, as in the health checks.
    const auto warm = [&](double t) {
        const auto c = at(coolant, t, 10);
        return !coolant || (c && *c >= 70);
    };
    if (rpm) {
        std::vector<double> idle;
        for (const Point &p : rpm->points) {
            const auto v = at(speed, p.t, 3);
            const bool standing = speed ? (v && *v < 2) : p.v < 1100;
            if (p.v > 400 && standing && warm(p.t))
                idle.push_back(p.v);
        }
        if (idle.size() >= 5)
            out << qMakePair(tr("Warm idle"), num(mean(idle), 0) + QStringLiteral(" rpm"));
    }
    const Column *shortTrim = column(data, 0x06);
    const Column *longTrim = column(data, 0x07);
    if (shortTrim || longTrim) {
        const Column *base = shortTrim ? shortTrim : longTrim;
        std::vector<double> trims;
        for (const Point &p : base->points) {
            const auto r = at(rpm, p.t, 3);
            if ((rpm && !(r && *r > 400)) || !warm(p.t))
                continue;
            double total = p.v;
            if (shortTrim && longTrim) {
                const auto lt = at(longTrim, p.t, 10);
                if (!lt)
                    continue;
                total += *lt;
            }
            trims.push_back(total);
        }
        if (trims.size() >= 5) {
            const double avg = mean(trims);
            out << qMakePair(tr("Fuel trim, bank 1"), (avg > 0 ? QStringLiteral("+") : QString()) + num(avg, 1)
                                                          + QStringLiteral(" %"));
        }
    }
    return out;
}

} // namespace recording
