#pragma once

#include "elm327.h"
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
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable CONSTANT)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString lastHost READ lastHost NOTIFY settingsChanged)
    Q_PROPERTY(int lastPort READ lastPort NOTIFY settingsChanged)
    Q_PROPERTY(QString lastBtAddress READ lastBtAddress NOTIFY settingsChanged)
    Q_PROPERTY(QString lastBtName READ lastBtName NOTIFY settingsChanged)
    Q_PROPERTY(bool lastBtBle READ lastBtBle NOTIFY settingsChanged)

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
    bool bluetoothAvailable() const;
    bool scanning() const;
    QVariantList devices() const { return m_devices; }
    QString lastHost() const;
    int lastPort() const;
    QString lastBtAddress() const;
    QString lastBtName() const;
    bool lastBtBle() const;

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
    void recordAllChanged();
    void recordingChanged();
    void recordingsChanged();
    void scanningChanged();
    void devicesChanged();
    void settingsChanged();

private:
    using TransportFactory = std::function<Transport *()>;

    void startTransport(const QString &name, TransportFactory factory);
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
    int pollIntervalMs(const SensorModel::Entry &e) const;
    void startRecording();
    void stopRecording();
    bool isCurrent(quint64 generation) const { return generation == m_generation; }

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
    int m_failStreak = 0;
    int m_samples = 0;
    double m_sampleRate = 0;
    int m_windowSeconds = 60;
    int m_viewTab = 0;

    void resetCodes();
    void finishCodes(const QString &status);
    bool m_dtcBusy = false;
    bool m_dtcRead = false;
    bool m_milOn = false;
    int m_milCount = 0;
    QString m_dtcStatus;
    QString m_dtcNote; // shown with the next read, e.g. after clearing
    QVariantList m_dtcCodes;
};
