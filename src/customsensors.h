#pragma once

#include <QByteArray>
#include <QJSEngine>
#include <QJSValue>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

// Sensors the user defines: any request the car answers (often mode 22,
// manufacturer data identifiers, with an ECU's CAN header) and a formula
// over the data bytes A..H, e.g. "(A*256+B)/4".
namespace custom {

struct Sensor {
    QString name;
    QString unit;
    QString header;  // CAN identifier to send to (7E0, 7E1, 18DA10F1...), empty for the default
    QString command; // hex, e.g. 221105
    QString formula;
    double min = 0;
    double max = 100;

    QVariantMap toMap() const;
    static Sensor fromMap(const QVariantMap &map);
};

// The sensors in the settings (`custom/sensors`).
QList<Sensor> load();
void save(const QList<Sensor> &sensors);

// What is wrong with a definition, or an empty string.
QString validate(const Sensor &sensor);

// Compiles formulas once and evaluates them on answers.
class Evaluator
{
public:
    Evaluator();

    // Index of the compiled formula, or -1 (and `error`) if it does not compile.
    int compile(const QString &formula, QString *error = nullptr);
    std::optional<double> evaluate(int index, const QByteArray &data);
    void clear() { m_functions.clear(); }

private:
    QJSEngine m_engine;
    QList<QJSValue> m_functions;
};

} // namespace custom
