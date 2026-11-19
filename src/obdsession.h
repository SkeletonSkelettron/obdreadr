#pragma once

#include "accel.h"
#include "alerts.h"
#include "battery.h"
#include "customsensors.h"
#include "elm327.h"
#include "fuelmeter.h"
#include "recorder.h"
#include "sensormodel.h"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringListModel>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

#ifdef OBD_HAVE_BLUETOOTH
#include <QBluetoothDeviceInfo>
class QBluetoothDeviceDiscoveryAgent;
#endif

class Transport;

// Connects to an adapter, finds the sensors the car supports and keeps
// polling the ones the user picked.
class ObdSession : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString connectionName READ connectionName NOTIFY infoChanged)
    Q_PROPERTY(QString adapterInfo READ adapterInfo NOTIFY infoChanged)
    Q_PROPERTY(QString protocol READ protocol NOTIFY infoChanged)
    Q_PROPERTY(QString vin READ vin NOTIFY infoChanged)
    Q_PROPERTY(QAbstractItemModel *sensors READ sensors CONSTANT)
    Q_PROPERTY(QAbstractItemModel *activeSensors READ activeSensors CONSTANT)
    Q_PROPERTY(QAbstractItemModel *pinnedSensors READ pinnedSensors CONSTANT)
    Q_PROPERTY(QList<QObject *> pinnedSeries READ pinnedSeries NOTIFY pinnedSeriesChanged)
    Q_PROPERTY(QAbstractItemModel *log READ log CONSTANT)
    Q_PROPERTY(int sensorCount READ sensorCount NOTIFY sensorCountChanged)
    Q_PROPERTY(double sampleRate READ sampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(int windowSeconds READ windowSeconds WRITE setWindowSeconds NOTIFY windowSecondsChanged)
    Q_PROPERTY(int viewTab READ viewTab WRITE setViewTab NOTIFY viewTabChanged)
    Q_PROPERTY(bool recordAll READ recordAll WRITE setRecordAll NOTIFY recordAllChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QVariantList recordings READ recordings NOTIFY recordingsChanged)
    Q_PROPERTY(bool dtcBusy READ dtcBusy NOTIFY dtcChanged)
    Q_PROPERTY(bool dtcRead READ dtcRead NOTIFY dtcChanged)
    Q_PROPERTY(bool milOn READ milOn NOTIFY dtcChanged)
    Q_PROPERTY(QString dtcStatus READ dtcStatus NOTIFY dtcChanged)
    Q_PROPERTY(QVariantList dtcCodes READ dtcCodes NOTIFY dtcChanged)
    Q_PROPERTY(QVariantList readiness READ readiness NOTIFY dtcChanged)
    Q_PROPERTY(QVariantMap freezeFrame READ freezeFrame NOTIFY dtcChanged)
    Q_PROPERTY(QVariantMap batteryTest READ batteryTest NOTIFY batteryTestChanged)
    Q_PROPERTY(QVariantList customSensors READ customSensors NOTIFY customSensorsChanged)
    Q_PROPERTY(QVariantList focusPids READ focusPids WRITE setFocusPids NOTIFY focusPidsChanged)
    Q_PROPERTY(bool accelActive READ accelActive WRITE setAccelActive NOTIFY accelChanged)
    Q_PROPERTY(QVariantMap accel READ accel NOTIFY accelChanged)
    Q_PROPERTY(QString reminderDevice READ reminderDevice NOTIFY settingsChanged)
    Q_PROPERTY(QString reminderDeviceName READ reminderDeviceName NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList pairedDevices READ pairedDevices NOTIFY pairedDevicesChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList healthChecks READ healthChecks NOTIFY healthChanged)
    Q_PROPERTY(bool testsBusy READ testsBusy NOTIFY testsChanged)
    Q_PROPERTY(bool testsRead READ testsRead NOTIFY testsChanged)
    Q_PROPERTY(QString testsStatus READ testsStatus NOTIFY testsChanged)
    Q_PROPERTY(QVariantList testResults READ testResults NOTIFY testsChanged)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString lastHost READ lastHost NOTIFY settingsChanged)
    Q_PROPERTY(int lastPort READ lastPort NOTIFY settingsChanged)
    Q_PROPERTY(QString lastBtAddress READ lastBtAddress NOTIFY settingsChanged)
    Q_PROPERTY(QString lastBtName READ lastBtName NOTIFY settingsChanged)
    Q_PROPERTY(bool lastBtBle READ lastBtBle NOTIFY settingsChanged)
    Q_PROPERTY(FuelMeter *fuel READ fuel CONSTANT)
    Q_PROPERTY(bool backgroundSupported READ backgroundSupported CONSTANT)
    Q_PROPERTY(bool background READ background WRITE setBackground NOTIFY backgroundChanged)
    Q_PROPERTY(bool batteryRestricted READ batteryRestricted NOTIFY backgroundChanged)
    Q_PROPERTY(bool reconnecting READ reconnecting NOTIFY stateChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY settingsChanged)
    Q_PROPERTY(bool keepScreenOn READ keepScreenOn WRITE setKeepScreenOn NOTIFY settingsChanged)
    Q_PROPERTY(bool alertsEnabled READ alertsEnabled WRITE setAlertsEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int alertRpm READ alertRpm WRITE setAlertRpm NOTIFY settingsChanged)

public:
    enum State { Disconnected, Connecting, Initializing, Discovering, Running, Failed };
    Q_ENUM(State)

    explicit ObdSession(QObject *parent = nullptr);
    ~ObdSession() override;

    State state() const { return m_state; }
    QString stateText() const;
    QString statusText() const { return m_statusText; }
    QString connectionName() const { return m_connectionName; }
    QString adapterInfo() const { return m_adapterInfo; }
    QString protocol() const { return m_protocol; }
    QString vin() const { return m_vin; }
    QAbstractItemModel *sensors() const { return m_sensors; }
    QAbstractItemModel *activeSensors() const { return m_activeSensors; }
    QAbstractItemModel *pinnedSensors() const { return m_pinnedSensors; }
    QList<QObject *> pinnedSeries() const;
    QAbstractItemModel *log() const { return m_log; }
    int sensorCount() const { return m_sensors->size(); }
    double sampleRate() const { return m_sampleRate; }
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    int windowSeconds() const { return m_windowSeconds; }
    void setWindowSeconds(int seconds);
    int viewTab() const { return m_viewTab; }
    void setViewTab(int tab);
    bool recordAll() const { return m_recordAll; }
    void setRecordAll(bool on);
    bool recording() const { return m_recorder.isRecording(); }
    QVariantList recordings() const { return m_recordings; }
    bool dtcBusy() const { return m_dtcBusy; }
    bool dtcRead() const { return m_dtcRead; }
    bool milOn() const { return m_milOn; }
    QString dtcStatus() const { return m_dtcStatus; }
    QVariantList dtcCodes() const { return m_dtcCodes; }
    QVariantList readiness() const { return m_readiness; }
    // The freeze frame (mode 02): {code, values: [{name, value}]}, read after
    // the codes when there are stored ones; empty when the car keeps none.
    QVariantMap freezeFrame() const { return m_freezeFrame; }
    QVariantList healthChecks() const { return m_healthChecks; }
    bool testsBusy() const { return m_testsBusy; }
    bool testsRead() const { return m_testsRead; }
    QString testsStatus() const { return m_testsStatus; }
    QVariantList testResults() const { return m_testResults; }
    bool bluetoothAvailable() const;
    bool scanning() const;
    QVariantList devices() const { return m_devices; }
    QString lastHost() const;
    int lastPort() const;
    QString lastBtAddress() const;
    QString lastBtName() const;
    bool lastBtBle() const;
    FuelMeter *fuel() const { return m_fuel; }

    // Background: on Android a foreground service keeps the app reading the
    // car with the screen off, and a lost connection is retried for a while
    // (setting `background/enabled`).
    bool backgroundSupported() const;
    bool background() const { return m_background; }
    void setBackground(bool on);
    bool batteryRestricted() const;
    bool reconnecting() const { return m_reconnectTimer.isActive(); }
    // Asks Android to stop optimizing the app's battery use.
    Q_INVOKABLE void allowBackground();

    // Connecting by itself: the last adapter when the app starts (setting
    // `connect/auto`), and the screen kept on while connected
    // (`view/keepScreenOn`).
    bool autoConnect() const { return m_autoConnect; }
    void setAutoConnect(bool on);
    bool keepScreenOn() const { return m_keepScreenOn; }
    void setKeepScreenOn(bool on);
    // Connects to the adapter used last (`connect/last`); false if there is none.
    Q_INVOKABLE bool connectLast();
    // Called when the start page appears; connects once per app start.
    Q_INVOKABLE void connectOnStart();

    // Alerts while driving (setting `alerts/enabled`), shown in the app and,
    // on Android, as a notification that sounds; `alerts/rpm` is an engine
    // speed limit, 0 for none.
    bool alertsEnabled() const { return m_alertsEnabled; }
    void setAlertsEnabled(bool on);
    int alertRpm() const { return m_alertRpm; }
    void setAlertRpm(int rpm);

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void connectBluetooth(const QString &address, const QString &name, bool ble);
    Q_INVOKABLE void connectWifi(const QString &host, int port);
    Q_INVOKABLE void connectDemo();
    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void disconnectAdapter();
    Q_INVOKABLE void setAllSensorsActive(bool active);
    Q_INVOKABLE void setAllSensorsPinned(bool pinned);

    // Fault codes: reads stored (03), pending (07) and permanent (0A) codes
    // and the check engine light; clearing (04) is the one standard write.
    Q_INVOKABLE void readCodes();
    Q_INVOKABLE void clearCodes();

    // The car's own test results (mode 06, CAN cars): how close each
    // monitored component is to failing, before a fault code is set.
    Q_INVOKABLE void readTests();
    // Re-runs the health checks on the latest sensor data.
    Q_INVOKABLE void evaluateHealth();

    // Battery test around starting the engine (Diagnostics → Battery): while
    // it runs only the voltage and the engine speed are read, in turn, as
    // fast as the adapter answers. batteryTest is {phase ("idle", "resting",
    // "crank", "running", "done"), volts, rest, restVerdict, cranking,
    // crankVerdict, charging, chargeVerdict, message}.
    QVariantMap batteryTest() const { return m_batteryResult; }
    Q_INVOKABLE void startBatteryTest();
    Q_INVOKABLE void cancelBatteryTest();

    // Custom sensors (setting `custom/sensors`): [{name, unit, header,
    // command, formula, min, max}]. Changes apply on the next connection.
    QVariantList customSensors() const;
    // An error message, or an empty string when the definition works.
    Q_INVOKABLE QString validateCustomSensor(const QVariantMap &sensor) const;
    // Adds (index -1) or replaces a sensor; returns an error or "".
    Q_INVOKABLE QString saveCustomSensor(int index, const QVariantMap &sensor);
    Q_INVOKABLE void removeCustomSensor(int index);

    // PIDs a page needs fast (the 0–100 timer, the HUD): read as if on
    // screen, also with "Record all sensors" off.
    QVariantList focusPids() const;
    void setFocusPids(const QVariantList &pids);
    // The series of a PID (-1: the adapter voltage), or null.
    Q_INVOKABLE QObject *seriesFor(int pid) const;
    // Full brightness for the head-up display, or back to the user's.
    Q_INVOKABLE void setFullBrightness(bool on);

    // The 0–100 km/h timer, fed while its page is open: {state ("idle",
    // "ready", "running", "done", "aborted"), elapsed, t60, t100, best100}.
    bool accelActive() const { return m_accelActive; }
    void setAccelActive(bool on);
    QVariantMap accel() const;
    Q_INVOKABLE void resetAccel();
    Q_INVOKABLE void clearAccelBest();

    // Car Bluetooth reminder (Android): when the picked device (the car's
    // stereo) connects, a notification offers to open the app, which then
    // connects by itself. Settings `reminder/address`, `reminder/name`.
    QString reminderDevice() const;
    QString reminderDeviceName() const;
    Q_INVOKABLE void setReminderDevice(const QString &address, const QString &name);
    QVariantList pairedDevices() const { return m_pairedDevices; }
    // Lists the phone's paired devices (asks for the Bluetooth permission).
    Q_INVOKABLE void loadPairedDevices();

    // The UI language: "" the phone's, "en", "ka" (setting `ui/language`).
    QString language() const;
    void setLanguage(const QString &code);

    // Recordings: one CSV file per connection, kept in the app's storage.
    Q_INVOKABLE void refreshRecordings();
    // Copies a recording to `target` (a file or, on Android, a content:// URL
    // from the system save dialog); returns a message for the user.
    Q_INVOKABLE QString exportRecording(const QString &path, const QUrl &target);
    Q_INVOKABLE void deleteRecording(const QString &path);

signals:
    void stateChanged();
    void statusTextChanged();
    void infoChanged();
    void sensorCountChanged();
    void sampleRateChanged();
    void pausedChanged();
    void windowSecondsChanged();
    void viewTabChanged();
    void pinnedSeriesChanged();
    void dtcChanged();
    void batteryTestChanged();
    void customSensorsChanged();
    void focusPidsChanged();
    void accelChanged();
    void pairedDevicesChanged();
    void languageChanged();
    void healthChanged();
    void testsChanged();
    void recordAllChanged();
    void recordingChanged();
    void recordingsChanged();
    void scanningChanged();
    void devicesChanged();
    void settingsChanged();
    void backgroundChanged();
    void alertRaised(const QString &title, const QString &text);

private:
    using TransportFactory = std::function<Transport *()>;

    // A new drive unless reconnecting to the same adapter.
    void startTransport(const QString &name, TransportFactory factory, bool newDrive = true);
    void teardown();
    void fail(const QString &message);
    void setState(State state);
    void setStatus(const QString &text);
    void appendLog(const QString &line);

    void onOpened();
    void discoverRange(quint8 basePid);
    void addSupported(quint8 basePid, quint32 mask);
    void finishDiscovery();
    void startPolling();
    void pollNext();
    void pollEntry(int row);
    void handlePollReply(int row, const Elm327::Reply &reply);
    // CAN cars: up to six sensors in one request.
    bool canBatch(const SensorModel::Entry &e) const;
    void pollBatch(const QList<int> &rows);
    // The digit telling the adapter how many answers to wait for, or empty.
    QString responseCount(int responders) const;
    void handleBatchReply(const QList<int> &rows, const Elm327::Reply &reply);
    void recordValue(int row, double t, double value);
    void countFailure(const QString &error);
    int pollIntervalMs(const SensorModel::Entry &e) const;
    QSet<int> availablePids() const;
    bool keepRunning() const;
    void startService();
    void updateService();
    void stopService();
    void scheduleReconnect();
    void rememberConnection(const QString &kind);
    void deliver(const QList<alerts::Alert> &raised);
    void pollCheckEngine();
    int rowOfPid(int pid) const;
    void pollBatteryStep();
    void updateBatteryTest(double t, const QString &message = QString());
    void updateScreenOn();
    void startRecording();
    void stopRecording();
    void discoverTests(quint8 baseMid);
    void queryTests(QList<quint8> mids);
    void resetDiagnostics();
    bool isCurrent(quint64 generation) const { return generation == m_generation; }

    FuelMeter *m_fuel;
    QSet<int> m_fuelPids;   // read for the fuel meter even when not on screen
    bool m_newDrive = true; // the next discovery starts "this drive" from zero
    bool m_background = true;
    bool m_serviceOn = false;
    QString m_serviceText;
    QTimer m_serviceTimer;   // refreshes the notification
    QTimer m_reconnectTimer;
    QElapsedTimer m_sinceData; // since the car last answered, for giving up on reconnecting
    bool m_autoConnect = true;
    bool m_startConnectDone = false;
    bool m_keepScreenOn = true;
    bool m_screenKeptOn = false;
    alerts::Monitor m_alerts;
    bool m_alertsEnabled = true;
    int m_alertRpm = 0;
    QTimer m_milTimer; // polls the check engine light for alerts
    std::optional<battery::Test> m_battery;
    bool m_batteryVoltsNext = true;
    double m_batteryVolts = -1;
    QVariantMap m_batteryResult;
    QSet<int> m_focusPids;
    accel::Timer m_accel;
    bool m_accelActive = false;
    QTimer m_accelTick; // the running time on screen
    QVariantList m_pairedDevices;

    void withBluetoothPermission(std::function<void()> action);
#ifdef OBD_HAVE_BLUETOOTH
    void onDeviceDiscovered(const QBluetoothDeviceInfo &info);
    QBluetoothDeviceDiscoveryAgent *m_agent = nullptr;
    QHash<QString, QBluetoothDeviceInfo> m_deviceInfos;
#endif

    State m_state = Disconnected;
    QString m_statusText;
    QString m_connectionName;
    QString m_adapterInfo;
    QString m_protocol;
    bool m_isCan = false;
    int m_canHeaderChars = 0; // 3 (11 bit) or 8 (29 bit) on CAN, for custom headers
    custom::Evaluator m_customFormulas;
    QString m_vin;
    SensorModel *m_sensors;
    SensorFilterModel *m_activeSensors;
    SensorFilterModel *m_pinnedSensors;
    QStringListModel *m_log;
    QVariantList m_devices;

    Elm327 m_elm;
    Transport *m_transport = nullptr;
    QString m_lastName;
    TransportFactory m_lastFactory;
    quint64 m_generation = 0;
    QElapsedTimer m_clock;
    QDateTime m_clockStart; // wall time when m_clock started
    Recorder m_recorder;
    bool m_recordAll = true;
    QVariantList m_recordings;
    QTimer m_connectTimeout;
    QTimer m_idleTimer;
    QTimer m_rateTimer;
    QSet<quint8> m_supported;

    bool m_paused = false;
    bool m_polling = false;
    bool m_batching = false; // CAN with headers on: several PIDs per request
    bool m_batchSingleFrame = false; // only as many PIDs as answer in one CAN frame
    int m_batchFailures = 0;
    bool m_responseCount = false; // the adapter takes "010C1" (see elm::takesResponseCount)
    int m_failStreak = 0;
    int m_samples = 0;
    double m_sampleRate = 0;
    int m_windowSeconds = 60;
    int m_viewTab = 0;

    void resetCodes();
    void readFreezeFrame(quint8 basePid = 0x00, QList<quint8> pids = {});
    void readFreezeFrameValues(const QList<quint8> &pids);
    void finishCodes(const QString &status);
    bool m_dtcBusy = false;
    bool m_dtcRead = false;
    bool m_milOn = false;
    int m_milCount = 0;
    QString m_dtcStatus;
    QString m_dtcNote; // shown with the next read, e.g. after clearing
    QVariantList m_dtcCodes;
    QVariantList m_readiness;
    QVariantMap m_freezeFrame;
    QVariantList m_healthChecks;
    QTimer m_healthTimer;
    QHash<int, SensorSeries *> m_seriesByPid; // -1 for the adapter voltage
    bool m_testsBusy = false;
    bool m_testsRead = false;
    QString m_testsStatus;
    QVariantList m_testResults;
    QList<quint8> m_testMids;
};
