#include "customsensors.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>

#include <cmath>

namespace custom {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("CustomSensors", text);
}

bool isHexText(const QString &text)
{
    static const QRegularExpression kHex(QStringLiteral("^[0-9A-Fa-f]+$"));
    return kHex.match(text).hasMatch();
}

} // namespace

QVariantMap Sensor::toMap() const
{
    return {
        {QStringLiteral("name"), name},       {QStringLiteral("unit"), unit},
        {QStringLiteral("header"), header},   {QStringLiteral("command"), command},
        {QStringLiteral("formula"), formula}, {QStringLiteral("min"), min},
        {QStringLiteral("max"), max},
    };
}

Sensor Sensor::fromMap(const QVariantMap &map)
{
    Sensor s;
    s.name = map.value(QStringLiteral("name")).toString().trimmed();
    s.unit = map.value(QStringLiteral("unit")).toString().trimmed();
    s.header = map.value(QStringLiteral("header")).toString().trimmed().toUpper();
    s.command = map.value(QStringLiteral("command")).toString().remove(QLatin1Char(' ')).toUpper();
    s.formula = map.value(QStringLiteral("formula")).toString().trimmed();
    s.min = map.value(QStringLiteral("min"), 0.0).toDouble();
    s.max = map.value(QStringLiteral("max"), 100.0).toDouble();
    return s;
}

QList<Sensor> load()
{
    QList<Sensor> sensors;
    const QVariantList list = QSettings().value(QStringLiteral("custom/sensors")).toList();
    for (const QVariant &item : list)
        sensors << Sensor::fromMap(item.toMap());
    return sensors;
}

void save(const QList<Sensor> &sensors)
{
    QVariantList list;
    for (const Sensor &s : sensors)
        list << s.toMap();
    QSettings().setValue(QStringLiteral("custom/sensors"), list);
}

QString validate(const Sensor &sensor)
{
    if (sensor.name.isEmpty())
        return tr("Give the sensor a name.");
    if (sensor.command.size() < 2 || sensor.command.size() % 2 != 0 || !isHexText(sensor.command))
        return tr("The command is hex bytes, e.g. 221105.");
    if (!sensor.header.isEmpty()
        && ((sensor.header.size() != 3 && sensor.header.size() != 8) || !isHexText(sensor.header)))
        return tr("The header is 3 hex digits (7E0) or 8 (18DA10F1), or empty.");
    if (sensor.formula.isEmpty())
        return tr("Enter a formula, e.g. A-40 or (A*256+B)/4.");
    if (!(sensor.max > sensor.min))
        return tr("The maximum must be above the minimum.");
    Evaluator evaluator;
    QString error;
    if (evaluator.compile(sensor.formula, &error) < 0)
        return error;
    if (!evaluator.evaluate(0, QByteArray(8, char(1))))
        return tr("The formula does not give a number.");
    return QString();
}

Evaluator::Evaluator() = default;

int Evaluator::compile(const QString &formula, QString *error)
{
    const QJSValue fn = m_engine.evaluate(
        QStringLiteral("(function(A, B, C, D, E, F, G, H) { return (%1); })").arg(formula));
    if (fn.isError() || !fn.isCallable()) {
        if (error)
            *error = tr("The formula does not work: %1").arg(fn.isError() ? fn.toString() : formula);
        return -1;
    }
    m_functions << fn;
    return int(m_functions.size() - 1);
}

std::optional<double> Evaluator::evaluate(int index, const QByteArray &data)
{
    if (index < 0 || index >= m_functions.size())
        return std::nullopt;
    QJSValueList args;
    for (int i = 0; i < 8; ++i)
        args << QJSValue(i < data.size() ? int(quint8(data.at(i))) : 0);
    const QJSValue result = m_functions[index].call(args);
    if (result.isError() || !result.isNumber())
        return std::nullopt;
    const double value = result.toNumber();
    if (!std::isfinite(value))
        return std::nullopt;
    return value;
}

} // namespace custom
