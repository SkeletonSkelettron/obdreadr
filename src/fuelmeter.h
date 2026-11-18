#pragma once

#include "fuel.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <deque>

class QSettings;
class QElapsedTimer;

// Fuel used and distance driven: right now, since connecting ("this
// drive"), in a measurement the user starts and stops, and in total over
// every drive, with the cost at the fuel price set. The measurement, the
// total, the current drive and the finished measurements and drives are kept
// in the settings, so they carry on across connections and restarts.
class FuelMeter : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The fuel meter belongs to ObdSession")

    Q_PROPERTY(QString method READ methodKey NOTIFY methodChanged)
    Q_PROPERTY(QString methodText READ methodText NOTIFY methodChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool hasNow READ hasNow NOTIFY changed)
    Q_PROPERTY(QString nowValue READ nowValue NOTIFY changed)
    Q_PROPERTY(QString nowUnit READ nowUnit NOTIFY changed)
    Q_PROPERTY(QString nowDetail READ nowDetail NOTIFY changed)
    Q_PROPERTY(bool tripRunning READ tripRunning NOTIFY changed)
    Q_PROPERTY(QVariantMap trip READ trip NOTIFY changed)
    Q_PROPERTY(QVariantMap drive READ drive NOTIFY changed)
    Q_PROPERTY(QVariantMap total READ total NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QVariantList drives READ drives NOTIFY changed)
    Q_PROPERTY(int fuelType READ fuelType WRITE setFuelType NOTIFY settingsChanged)
    Q_PROPERTY(double engineLiters READ engineLiters WRITE setEngineLiters NOTIFY settingsChanged)
    Q_PROPERTY(int correction READ correction WRITE setCorrection NOTIFY settingsChanged)
    Q_PROPERTY(double price READ price WRITE setPrice NOTIFY settingsChanged)
    Q_PROPERTY(bool onScreen READ onScreen WRITE setOnScreen NOTIFY onScreenChanged)

public:
    // `clock` is the session clock the sample times count from.
    explicit FuelMeter(const QElapsedTimer *clock, QObject *parent = nullptr);
    ~FuelMeter() override;

    fuel::Method method() const { return m_method; }
    QString methodKey() const;
    QString methodText() const;
    bool connected() const { return m_connected; }
    bool hasNow() const { return now().valid; }
    QString nowValue() const;
    QString nowUnit() const;
    QString nowDetail() const;
    bool tripRunning() const { return m_tripRunning; }
    QVariantMap trip() const;
    QVariantMap drive() const;
    QVariantMap total() const;
    QVariantList history() const;
    // Finished drives, newest first, like the history.
    QVariantList drives() const;
    int fuelType() const { return int(m_kind); }
    void setFuelType(int type);
    double engineLiters() const { return m_engineLiters; }
    void setEngineLiters(double liters);
    int correction() const { return m_correction; }
    void setCorrection(int percent);
    // Lari per litre; 0 shows no cost.
    double price() const { return m_price; }
    void setPrice(double gelPerLiter);
    bool onScreen() const { return m_onScreen; }
    void setOnScreen(bool on);

    // Raw figures (before the correction), for the tests.
    const fuel::Totals &driveTotals() const { return m_drive; }
    const fuel::Totals &tripTotals() const { return m_trip; }
    const fuel::Totals &totalTotals() const { return m_total; }

    // Called by ObdSession: a connection reached the car and reports `pids`;
    // `newDrive` finishes the last drive and starts "this drive" from zero
    // (not after an automatic reconnect).
    void startSession(const QSet<int> &pids, bool newDrive);
    // A sensor stopped answering.
    void setAvailablePids(const QSet<int> &pids);
    void endSession();
    // One reading of a mode 01 PID at session time t (seconds).
    void addSample(int pid, double t, double value);
    // The PIDs the meter needs read, even when they are not on screen.
    QList<int> inputs() const { return m_inputs; }
    // A few short lines for the background notification.
    QStringList summary() const;
    void save();

    Q_INVOKABLE void startTrip();
    Q_INVOKABLE void stopTrip();
    Q_INVOKABLE void resetTotal();
    Q_INVOKABLE void clearHistory();
    // Ends "this drive" and adds it to the drives, when it was long enough;
    // ObdSession calls it when the user disconnects.
    Q_INVOKABLE void finishDrive();
    Q_INVOKABLE void clearDrives();

signals:
    void changed();
    void methodChanged();
    void settingsChanged();
    void onScreenChanged();
    void inputsChanged();

private:
    struct Reading {
        double t = -1e9;
        double v = 0;
        bool fresh(double now, double maxAge) const { return now - t <= maxAge; }
    };
    struct Snapshot {
        double t;
        double km;
        double liters;
    };
    struct Now {
        bool valid = false;
        double value = 0;         // corrected
        bool perDistance = false; // L/100 km rather than L/h
        double kmh = 0;           // average speed over the same seconds
        double litersPerHour = 0; // average fuel flow, corrected
    };

    Now now() const;
    double clockNow() const;
    double factor() const { return m_correction / 100.0; }
    void chooseMethod();
    void addRate(double t, double litersPerHour);
    void addSpeedDensity(double t);
    void add(double km, double liters, double seconds);
    void snapshot(double t);
    void touch();
    void markDirty();
    void updateTick();
    QVariantMap totalsMap(const fuel::Totals &totals, double seconds) const;
    QVariantList listMap(const QVariantList &raw, bool movingTime) const;
    void finishDriveAt(const QDateTime &fallbackEnd);
    static fuel::Totals loadTotals(const QSettings &settings, const QString &group);
    static void saveTotals(QSettings &settings, const QString &group, const fuel::Totals &totals);

    const QElapsedTimer *m_clock;
    fuel::Kind m_kind = fuel::Kind::Petrol;
    double m_engineLiters = 1.6;
    int m_correction = 100;
    double m_price = 0;
    bool m_onScreen = false;

    bool m_connected = false;
    QSet<int> m_pids;
    fuel::Method m_method = fuel::Method::None;
    QList<int> m_inputs;

    fuel::Integrator m_speedIntegrator;
    fuel::Integrator m_rateIntegrator;
    Reading m_speed;
    Reading m_rate;
    Reading m_map;
    Reading m_rpm;
    Reading m_intakeTemp;
    Reading m_lambda;
    std::deque<Snapshot> m_window; // the last seconds of this drive, for "now"

    fuel::Totals m_drive;
    fuel::Totals m_trip;
    fuel::Totals m_total;
    bool m_tripRunning = false;
    QDateTime m_tripStart;
    QDateTime m_tripEnd;
    QDateTime m_totalSince;
    QVariantList m_history; // finished measurements, newest first, raw figures
    QDateTime m_driveStart;
    QDateTime m_driveLast;  // when the drive last counted something
    QVariantList m_drives;  // finished drives, newest first, raw figures

    bool m_dirty = false;
    QTimer m_notifyTimer; // batches changed() to a few times a second
    QTimer m_saveTimer;
    QTimer m_tickTimer;   // the running measurement's clock on screen
};
