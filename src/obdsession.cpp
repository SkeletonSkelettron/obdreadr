#include "obdsession.h"

#include "obd/elmparser.h"
#include "obd/pids.h"
#include "sensorseries.h"
#include "transport/simtransport.h"
#include "transport/tcptransport.h"

#include <QCoreApplication>
#include <QSettings>

#include <algorithm>

#ifdef OBD_HAVE_BLUETOOTH
#include "transport/bletransport.h"
#include "transport/btclassictransport.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#endif

#if QT_CONFIG(permissions)
#include <QPermissions>
#endif

namespace {

constexpr int kConnectTimeoutMs = 20000;
constexpr int kVoltageIntervalMs = 2000;
constexpr int kMissesBeforeUnavailable = 6;  // NO DATA answers in a row for one sensor
constexpr int kFailuresBeforeLost = 12;      // failed answers in a row for all sensors
constexpr int kMaxLogLines = 500;

const char kVoltageKey[] = "ATRV";

QString modeKey(quint8 pid)
{
    return QStringLiteral("01:") + obd::hex2(pid);
}

#ifdef OBD_HAVE_BLUETOOTH
bool looksLikeObd(const QString &name)
{
    static const char *kHints[] = {"OBD", "ELM", "LINK", "VGATE", "ICAR", "KONNWEI", "VEEPEAK", "CARISTA", "BAFX"};
    const QString upper = name.toUpper();
    return std::any_of(std::begin(kHints), std::end(kHints),
                       [&](const char *hint) { return upper.contains(QLatin1String(hint)); });
}
#endif

} // namespace

ObdSession::ObdSession(QObject *parent)
    : QObject(parent)
    , m_sensors(new SensorModel(this))
    , m_activeSensors(new ActiveSensorModel(this))
    , m_log(new QStringListModel(this))
{
    m_activeSensors->setSourceModel(m_sensors);

    QSettings settings;
    m_windowSeconds = settings.value(QStringLiteral("view/window"), 60).toInt();

    connect(&m_elm, &Elm327::logLine, this, &ObdSession::appendLog);
    connect(m_sensors, &SensorModel::activeChanged, this, [this] {
        if (m_sensors->size() > 0)
            QSettings().setValue(QStringLiteral("sensors/active"), m_sensors->activeKeys());
        if (m_state == Running)
            pollNext();
    });

    m_connectTimeout.setSingleShot(true);
    connect(&m_connectTimeout, &QTimer::timeout, this,
            [this] { fail(tr("Could not connect to %1 (timed out)").arg(m_connectionName)); });

    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &ObdSession::pollNext);

    m_rateTimer.setInterval(1000);
    connect(&m_rateTimer, &QTimer::timeout, this, [this] {
        m_sampleRate = m_samples;
        m_samples = 0;
        emit sampleRateChanged();
    });
}

ObdSession::~ObdSession()
{
    teardown();
}

QString ObdSession::stateText() const
{
    switch (m_state) {
    case Disconnected: return tr("Disconnected");
    case Connecting: return tr("Connecting…");
    case Initializing: return tr("Initializing adapter…");
    case Discovering: return tr("Discovering sensors…");
    case Running: return m_paused ? tr("Paused") : tr("Live");
    case Failed: return tr("Error");
    }
    return {};
}

void ObdSession::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    emit pausedChanged();
    emit stateChanged(); // stateText
    if (!paused)
        pollNext();
}

void ObdSession::setWindowSeconds(int seconds)
{
    seconds = std::clamp(seconds, 10, 900);
    if (m_windowSeconds == seconds)
        return;
    m_windowSeconds = seconds;
    QSettings().setValue(QStringLiteral("view/window"), seconds);
    emit windowSecondsChanged();
}

QString ObdSession::lastHost() const
{
    return QSettings().value(QStringLiteral("wifi/host"), QStringLiteral("192.168.0.10")).toString();
}

int ObdSession::lastPort() const
{
    return QSettings().value(QStringLiteral("wifi/port"), 35000).toInt();
}

QString ObdSession::lastBtAddress() const
{
    return QSettings().value(QStringLiteral("bt/address")).toString();
}

QString ObdSession::lastBtName() const
{
    return QSettings().value(QStringLiteral("bt/name")).toString();
}

bool ObdSession::lastBtBle() const
{
    return QSettings().value(QStringLiteral("bt/ble"), false).toBool();
}

void ObdSession::setAllSensorsActive(bool active)
{
    m_sensors->setAllActive(active);
}

// --- Connecting -------------------------------------------------------------

void ObdSession::connectDemo()
{
    startTransport(tr("Demo car"), [this] { return new SimTransport(this); });
}

void ObdSession::connectWifi(const QString &host, int port)
{
    if (host.trimmed().isEmpty() || port <= 0 || port > 65535) {
        setStatus(tr("Enter the adapter's IP address and port"));
        return;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("wifi/host"), host.trimmed());
    settings.setValue(QStringLiteral("wifi/port"), port);
    emit settingsChanged();
    const QString h = host.trimmed();
    startTransport(QStringLiteral("%1:%2").arg(h).arg(port),
                   [this, h, port] { return new TcpTransport(h, quint16(port), this); });
}

void ObdSession::connectBluetooth(const QString &address, const QString &name, bool ble)
{
#ifdef OBD_HAVE_BLUETOOTH
    const QBluetoothAddress addr(address.trimmed().toUpper());
    if (addr.isNull()) {
        setStatus(tr("\"%1\" is not a Bluetooth address").arg(address));
        return;
    }
    withBluetoothPermission([this, addr, name, ble] {
        stopScan(); // discovery slows down or breaks connecting on Android
        QSettings settings;
        settings.setValue(QStringLiteral("bt/address"), addr.toString());
        settings.setValue(QStringLiteral("bt/name"), name);
        settings.setValue(QStringLiteral("bt/ble"), ble);
        emit settingsChanged();

        const QString label = name.isEmpty() ? addr.toString() : name;
        if (ble) {
            QBluetoothDeviceInfo info = m_deviceInfos.value(addr.toString());
            if (!info.isValid()) {
                info = QBluetoothDeviceInfo(addr, name, 0);
                info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
            }
            startTransport(label, [this, info] { return new BleTransport(info, this); });
        } else {
            startTransport(label, [this, addr] { return new BtClassicTransport(addr, this); });
        }
    });
#else
    Q_UNUSED(address)
    Q_UNUSED(name)
    Q_UNUSED(ble)
    setStatus(tr("This build has no Bluetooth support"));
#endif
}

void ObdSession::reconnect()
{
    if (m_lastFactory)
        startTransport(m_lastName, m_lastFactory);
}

void ObdSession::disconnectAdapter()
{
    teardown();
    setState(Disconnected);
    setStatus(QString());
}

void ObdSession::startTransport(const QString &name, TransportFactory factory)
{
    teardown();
    const quint64 generation = m_generation;

    m_lastName = name;
    m_lastFactory = factory;
    m_connectionName = name;
    m_adapterInfo.clear();
    m_protocol.clear();
    m_vin.clear();
    m_supported.clear();
    m_paused = false;
    emit pausedChanged();
    emit infoChanged();
    m_sensors->clear();
    emit sensorCountChanged();
    m_log->setStringList({});
    m_clock.start();

    m_transport = factory();
    connect(m_transport, &Transport::opened, this, [this, generation] {
        if (isCurrent(generation))
            onOpened();
    });
    connect(m_transport, &Transport::errorOccurred, this, [this, generation](const QString &message) {
        if (isCurrent(generation))
            fail(message);
    });
    connect(m_transport, &Transport::closed, this, [this, generation] {
        if (isCurrent(generation))
            fail(tr("The adapter closed the connection"));
    });
    m_elm.setTransport(m_transport);

    appendLog(tr("Connecting to %1").arg(name));
    setStatus(tr("Connecting to %1…").arg(name));
    setState(Connecting);
    m_connectTimeout.start(kConnectTimeoutMs);
    m_transport->open();
}

void ObdSession::teardown()
{
    ++m_generation;
    m_connectTimeout.stop();
    m_idleTimer.stop();
    m_rateTimer.stop();
    m_polling = false;
    m_elm.setTransport(nullptr);
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->close();
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    if (m_sampleRate != 0) {
        m_sampleRate = 0;
        emit sampleRateChanged();
    }
}

void ObdSession::fail(const QString &message)
{
    teardown();
    appendLog(QStringLiteral("! ") + message);
    setStatus(message);
    setState(Failed);
}

void ObdSession::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void ObdSession::setStatus(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void ObdSession::appendLog(const QString &line)
{
    const double seconds = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;
    const int row = m_log->rowCount();
    m_log->insertRows(row, 1);
    m_log->setData(m_log->index(row), QStringLiteral("%1  %2").arg(seconds, 7, 'f', 2).arg(line));
    if (m_log->rowCount() > kMaxLogLines)
        m_log->removeRows(0, m_log->rowCount() - kMaxLogLines);
}

// --- Initialization and discovery ------------------------------------------

void ObdSession::onOpened()
{
    m_connectTimeout.stop();
    setState(Initializing);
    setStatus(tr("Initializing the ELM327 adapter…"));
    m_elm.setVerbose(true);

    const quint64 generation = m_generation;
    m_elm.send(QStringLiteral("ATZ"), [this, generation](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        for (const QString &line : reply.lines) {
            if (line.contains(QLatin1String("ELM")) || line.contains(QLatin1String("STN"))) {
                m_adapterInfo = line;
                break;
            }
        }
        emit infoChanged();
    }, 5000);

    // Echo, linefeeds, spaces and headers off; adaptive timing; automatic protocol.
    for (const char *command : {"ATE0", "ATL0", "ATS0", "ATH0", "ATAT1", "ATSP0"})
        m_elm.send(QString::fromLatin1(command));

    m_elm.send(QStringLiteral("ATRV"), [this, generation](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        if (const auto volts = elm::parseVoltage(reply.lines))
            appendLog(tr("Battery: %1 V").arg(*volts, 0, 'f', 1));
        setStatus(tr("Waiting for the car's ECU (the ignition must be on)…"));
    });

    // The first request makes the adapter search for the car's protocol,
    // which can take several seconds.
    m_elm.send(QStringLiteral("0100"), [this, generation](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        const auto mask = elm::supportedMask(reply.lines, 0x00);
        if (!mask) {
            const QString why = reply.error.isEmpty() ? tr("no answer") : reply.error;
            fail(tr("The car did not answer (%1). Is the ignition on?").arg(why));
            return;
        }
        m_elm.send(QStringLiteral("ATDPN"), [this, generation](const Elm327::Reply &r) {
            if (!isCurrent(generation) || r.lines.isEmpty())
                return;
            m_protocol = elm::protocolName(r.lines.first());
            emit infoChanged();
        });
        setState(Discovering);
        setStatus(tr("Discovering sensors…"));
        addSupported(0x00, *mask);
        if (*mask & 1u)
            discoverRange(0x20);
        else
            finishDiscovery();
    }, 20000);
}

void ObdSession::discoverRange(quint8 basePid)
{
    const quint64 generation = m_generation;
    m_elm.send(QStringLiteral("01") + obd::hex2(basePid), [this, generation, basePid](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        const auto mask = elm::supportedMask(reply.lines, basePid);
        if (mask)
            addSupported(basePid, *mask);
        if (mask && (*mask & 1u) && basePid < 0xC0)
            discoverRange(quint8(basePid + 0x20));
        else
            finishDiscovery();
    }, 5000);
}

void ObdSession::addSupported(quint8 basePid, quint32 mask)
{
    const QList<quint8> pids = obd::pidsFromMask(mask, basePid);
    for (quint8 pid : pids)
        m_supported.insert(pid);
}

void ObdSession::finishDiscovery()
{
    QList<quint8> pids(m_supported.begin(), m_supported.end());
    std::sort(pids.begin(), pids.end());

    std::vector<SensorModel::Entry> entries;
    QStringList charted;
    QStringList other;

    SensorModel::Entry voltage;
    voltage.key = QString::fromLatin1(kVoltageKey);
    voltage.favorite = true;
    voltage.series = new SensorSeries(tr("Battery voltage (adapter)"), QStringLiteral("V"), 1, 0, 16, &m_clock, this);
    entries.push_back(voltage);

    for (quint8 pid : std::as_const(pids)) {
        if (obd::isSupportMaskPid(pid))
            continue;
        const obd::PidInfo *info = obd::findPid(pid);
        if (!info) {
            other << obd::hex2(pid);
            continue;
        }
        SensorModel::Entry e;
        e.key = modeKey(pid);
        e.pid = pid;
        e.favorite = info->favorite;
        e.series = new SensorSeries(QString::fromUtf8(info->name), QString::fromUtf8(info->unit), info->decimals,
                                    info->min, info->max, &m_clock, this);
        entries.push_back(e);
        charted << obd::hex2(pid);
    }

    // Keep the user's previous choice if it fits this car, else the favorites.
    const QStringList saved = QSettings().value(QStringLiteral("sensors/active")).toStringList();
    const bool useSaved = std::any_of(entries.begin(), entries.end(), [&](const SensorModel::Entry &e) {
        return e.pid >= 0 && saved.contains(e.key);
    });
    for (SensorModel::Entry &e : entries)
        e.active = useSaved ? saved.contains(e.key) : e.favorite;

    m_sensors->setEntries(std::move(entries));
    emit sensorCountChanged();
    appendLog(tr("Sensors: %1").arg(charted.join(QLatin1Char(' '))));
    if (!other.isEmpty())
        appendLog(tr("Supported but not charted (status PIDs): %1").arg(other.join(QLatin1Char(' '))));

    const quint64 generation = m_generation;
    m_elm.send(QStringLiteral("0902"), [this, generation](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        m_vin = elm::parseVin(reply.lines);
        if (!m_vin.isEmpty()) {
            appendLog(QStringLiteral("VIN: ") + m_vin);
            emit infoChanged();
        }
        startPolling();
    }, 5000);
}

// --- Polling -----------------------------------------------------------------

void ObdSession::startPolling()
{
    m_elm.setVerbose(false);
    m_failStreak = 0;
    m_samples = 0;
    m_pollIndex = -1;
    m_rateTimer.start();
    setState(Running);
    setStatus(tr("%n sensor(s) found", nullptr, m_sensors->size() - 1));
    appendLog(tr("Polling started"));
    pollNext();
}

void ObdSession::pollNext()
{
    if (m_state != Running || m_paused || m_polling)
        return;
    const int count = m_sensors->size();
    const qint64 nowMs = m_clock.elapsed();
    for (int step = 0; step < count; ++step) {
        m_pollIndex = (m_pollIndex + 1) % count;
        const SensorModel::Entry &e = m_sensors->entry(m_pollIndex);
        if (!e.active || !e.available)
            continue;
        if (e.pid < 0 && nowMs - e.lastPollMs < kVoltageIntervalMs)
            continue;
        pollEntry(m_pollIndex);
        return;
    }
    // Nothing is due (all sensors off, or only the voltage is waiting).
    m_idleTimer.start(200);
}

void ObdSession::pollEntry(int row)
{
    SensorModel::Entry &e = m_sensors->entry(row);
    e.lastPollMs = m_clock.elapsed();
    const QString command = e.pid < 0 ? QString::fromLatin1(kVoltageKey) : QStringLiteral("01") + obd::hex2(quint8(e.pid));
    const quint64 generation = m_generation;
    m_polling = true;
    m_elm.send(command, [this, generation, row](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        m_polling = false;
        handlePollReply(row, reply);
        if (m_state == Running)
            QMetaObject::invokeMethod(this, &ObdSession::pollNext, Qt::QueuedConnection);
    });
}

void ObdSession::handlePollReply(int row, const Elm327::Reply &reply)
{
    SensorModel::Entry &e = m_sensors->entry(row);
    std::optional<double> value;
    if (e.pid < 0) {
        value = elm::parseVoltage(reply.lines);
    } else if (const auto payload = elm::mode01Payload(reply.lines, quint8(e.pid))) {
        value = obd::decode(quint8(e.pid), *payload);
    }

    if (value) {
        e.series->append(m_clock.elapsed() / 1000.0, *value);
        e.misses = 0;
        m_failStreak = 0;
        ++m_samples;
        return;
    }

    // A sensor the car stopped answering is dropped from the dashboard;
    // timeouts and bus errors say nothing about the sensor itself.
    const bool sensorProblem = reply.error.isEmpty() || reply.error == QLatin1String("NO DATA");
    if (sensorProblem && e.pid >= 0 && ++e.misses >= kMissesBeforeUnavailable) {
        appendLog(tr("%1: no data, removed from the dashboard").arg(e.series->name()));
        m_sensors->setAvailable(row, false);
    }

    if (++m_failStreak >= kFailuresBeforeLost) {
        const QString why = reply.error.isEmpty() ? tr("no valid data") : reply.error;
        fail(tr("Lost contact with the car (%1)").arg(why));
    }
}

// --- Bluetooth -----------------------------------------------------------------

bool ObdSession::bluetoothAvailable() const
{
#ifdef OBD_HAVE_BLUETOOTH
    return true;
#else
    return false;
#endif
}

bool ObdSession::scanning() const
{
#ifdef OBD_HAVE_BLUETOOTH
    return m_agent && m_agent->isActive();
#else
    return false;
#endif
}

void ObdSession::withBluetoothPermission(std::function<void()> action)
{
#if QT_CONFIG(permissions) && defined(OBD_HAVE_BLUETOOTH)
    QBluetoothPermission permission;
    permission.setCommunicationModes(QBluetoothPermission::Access);
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this, [this, action](const QPermission &result) {
            if (result.status() == Qt::PermissionStatus::Granted)
                action();
            else
                setStatus(tr("Bluetooth permission was denied"));
        });
        return;
    case Qt::PermissionStatus::Denied:
        setStatus(tr("Bluetooth permission was denied. Allow \"Nearby devices\" for this app in Android settings."));
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
#endif
    action();
}

void ObdSession::startScan()
{
#ifdef OBD_HAVE_BLUETOOTH
    withBluetoothPermission([this] {
        QBluetoothLocalDevice local;
        if (!local.isValid()) {
            setStatus(tr("This device has no Bluetooth"));
            return;
        }
        if (local.hostMode() == QBluetoothLocalDevice::HostPoweredOff) {
            local.powerOn();
            setStatus(tr("Turning Bluetooth on… press Scan again"));
            return;
        }
        if (!m_agent) {
            m_agent = new QBluetoothDeviceDiscoveryAgent(this);
            m_agent->setLowEnergyDiscoveryTimeout(10000);
            connect(m_agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &ObdSession::onDeviceDiscovered);
            connect(m_agent, &QBluetoothDeviceDiscoveryAgent::finished, this, [this] {
                emit scanningChanged();
                if (m_state == Disconnected || m_state == Failed)
                    setStatus(tr("Scan finished: %n device(s)", nullptr, int(m_devices.size())));
            });
            connect(m_agent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &ObdSession::scanningChanged);
            connect(m_agent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [this] {
                setStatus(tr("Bluetooth scan failed: %1").arg(m_agent->errorString()));
                emit scanningChanged();
            });
        }
        m_devices.clear();
        m_deviceInfos.clear();
        emit devicesChanged();
        m_agent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod | QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
        setStatus(tr("Scanning for Bluetooth devices…"));
        emit scanningChanged();
    });
#else
    setStatus(tr("This build has no Bluetooth support"));
#endif
}

void ObdSession::stopScan()
{
#ifdef OBD_HAVE_BLUETOOTH
    if (m_agent && m_agent->isActive()) {
        m_agent->stop();
        emit scanningChanged();
    }
#endif
}

#ifdef OBD_HAVE_BLUETOOTH
void ObdSession::onDeviceDiscovered(const QBluetoothDeviceInfo &info)
{
    const QBluetoothAddress address = info.address();
    if (address.isNull())
        return;
    const auto config = info.coreConfigurations();
    const bool ble = (config & QBluetoothDeviceInfo::LowEnergyCoreConfiguration)
                     && !(config & QBluetoothDeviceInfo::BaseRateCoreConfiguration);
    const QString key = address.toString();
    m_deviceInfos.insert(key, info);

    QVariantMap device;
    device.insert(QStringLiteral("name"), info.name().isEmpty() ? tr("(no name)") : info.name());
    device.insert(QStringLiteral("address"), key);
    device.insert(QStringLiteral("ble"), ble);
    device.insert(QStringLiteral("likelyObd"), looksLikeObd(info.name()));

    auto existing = std::find_if(m_devices.begin(), m_devices.end(), [&](const QVariant &v) {
        return v.toMap().value(QStringLiteral("address")).toString() == key;
    });
    if (existing != m_devices.end())
        *existing = device;
    else
        m_devices << device;

    std::stable_sort(m_devices.begin(), m_devices.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("likelyObd")).toBool() > b.toMap().value(QStringLiteral("likelyObd")).toBool();
    });
    emit devicesChanged();
}
#endif
