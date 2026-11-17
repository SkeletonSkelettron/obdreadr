#include "obdsession.h"

#include "obd/elmparser.h"
#include "health.h"
#include "obd/dtc.h"
#include "obd/monitors.h"
#include "obd/pids.h"
#include "sensorseries.h"
#include "transport/simtransport.h"
#include "transport/tcptransport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>

#include <algorithm>
#include <iterator>
#include <utility>

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
    , m_activeSensors(new SensorFilterModel(SensorModel::ActiveRole, this))
    , m_pinnedSensors(new SensorFilterModel(SensorModel::PinnedRole, this))
    , m_log(new QStringListModel(this))
{
    m_activeSensors->setSourceModel(m_sensors);
    m_pinnedSensors->setSourceModel(m_sensors);

    QSettings settings;
    m_windowSeconds = settings.value(QStringLiteral("view/window"), 60).toInt();
    m_viewTab = settings.value(QStringLiteral("view/tab"), 0).toInt();
    m_recordAll = settings.value(QStringLiteral("record/all"), true).toBool();
    refreshRecordings();

    connect(&m_elm, &Elm327::logLine, this, &ObdSession::appendLog);
    connect(m_sensors, &SensorModel::selectionChanged, this, [this] {
        if (m_sensors->size() > 0) {
            QSettings s;
            s.setValue(QStringLiteral("sensors/active"), m_sensors->keys(SensorModel::ActiveRole));
            s.setValue(QStringLiteral("sensors/pinned"), m_sensors->keys(SensorModel::PinnedRole));
        }
        emit pinnedSeriesChanged();
        if (m_state == Running)
            pollNext();
    });

    m_connectTimeout.setSingleShot(true);
    connect(&m_connectTimeout, &QTimer::timeout, this,
            [this] { fail(tr("Could not connect to %1 (timed out)").arg(m_connectionName)); });

    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &ObdSession::pollNext);

    m_healthTimer.setInterval(2000);
    connect(&m_healthTimer, &QTimer::timeout, this, &ObdSession::evaluateHealth);

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
    for (int row = 0; row < m_sensors->size(); ++row)
        m_sensors->entry(row).series->setRangeWindow(seconds);
    emit windowSecondsChanged();
}

void ObdSession::setViewTab(int tab)
{
    if (m_viewTab == tab)
        return;
    m_viewTab = tab;
    QSettings().setValue(QStringLiteral("view/tab"), tab);
    emit viewTabChanged();
}

QList<QObject *> ObdSession::pinnedSeries() const
{
    QList<QObject *> series;
    for (int row = 0; row < m_sensors->size(); ++row) {
        const SensorModel::Entry &e = m_sensors->entry(row);
        if (e.pinned && e.available)
            series << e.series;
    }
    return series;
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
    m_sensors->setAll(SensorModel::ActiveRole, active);
}

void ObdSession::setAllSensorsPinned(bool pinned)
{
    m_sensors->setAll(SensorModel::PinnedRole, pinned);
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
    resetCodes();
    resetDiagnostics();
    m_clock.start();
    m_clockStart = QDateTime::currentDateTime();

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
    stopRecording();
    m_healthTimer.stop();
    if (m_testsBusy) {
        m_testsBusy = false;
        emit testsChanged();
    }
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->close();
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    if (m_dtcBusy) {
        m_dtcBusy = false;
        emit dtcChanged();
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
            m_isCan = elm::isCanProtocol(r.lines.first());
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

    // Each sensor keeps one color in every tab.
    const auto nextColor = [&entries] { return SensorSeries::paletteColor(int(entries.size())); };

    SensorModel::Entry voltage;
    voltage.key = QString::fromLatin1(kVoltageKey);
    voltage.favorite = true;
    voltage.series = new SensorSeries(tr("Battery voltage (adapter)"), QStringLiteral("V"), 1, 0, 16,
                                      nextColor(), &m_clock, this);
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
                                    info->min, info->max, nextColor(), &m_clock, this);
        entries.push_back(e);
        charted << obd::hex2(pid);
    }

    // Keep the user's previous choices if they fit this car, else the defaults.
    const auto restore = [&entries](const QString &settingsKey, const auto &byDefault) {
        const QStringList saved = QSettings().value(settingsKey).toStringList();
        const bool useSaved = std::any_of(entries.begin(), entries.end(), [&](const SensorModel::Entry &e) {
            return e.pid >= 0 && saved.contains(e.key);
        });
        QList<bool> flags;
        for (const SensorModel::Entry &e : entries)
            flags << (useSaved ? saved.contains(e.key) : byDefault(e));
        return flags;
    };
    const QList<bool> active = restore(QStringLiteral("sensors/active"),
                                       [](const SensorModel::Entry &e) { return e.favorite; });
    const QList<bool> pinned = restore(QStringLiteral("sensors/pinned"), [](const SensorModel::Entry &e) {
        return e.pid == 0x0C || e.pid == 0x0D || e.pid == 0x11 || e.pid == 0x04;
    });
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].active = active[qsizetype(i)];
        entries[i].pinned = pinned[qsizetype(i)];
        entries[i].series->setRangeWindow(m_windowSeconds);
    }

    m_sensors->setEntries(std::move(entries));
    m_seriesByPid.clear();
    for (int row = 0; row < m_sensors->size(); ++row)
        m_seriesByPid.insert(m_sensors->entry(row).pid, m_sensors->entry(row).series);
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
    m_rateTimer.start();
    startRecording();
    m_healthTimer.start();
    setState(Running);
    setStatus(tr("%n sensor(s) found", nullptr, m_sensors->size() - 1));
    appendLog(tr("Polling started"));
    pollNext();
}

int ObdSession::pollIntervalMs(const SensorModel::Entry &e) const
{
    // Sensors on screen as often as the adapter allows (fast ones) or every
    // second or two; the rest, recorded in the background, less often.
    const obd::Pace pace = e.pid < 0 ? obd::Pace::Medium : obd::pace(quint8(e.pid));
    switch (pace) {
    case obd::Pace::Fast: return e.shown() ? 100 : 1000;
    case obd::Pace::Medium: return e.shown() ? 1000 : 2000;
    case obd::Pace::Slow: return e.shown() ? 2000 : 5000;
    }
    return 1000;
}

void ObdSession::pollNext()
{
    if (m_state != Running || m_paused || m_polling)
        return;
    // Ask for the sensor that is most overdue relative to its interval, so
    // that when the adapter cannot keep up every sensor slows down evenly.
    const qint64 nowMs = m_clock.elapsed();
    int best = -1;
    double bestRatio = 1.0;
    qint64 nextDueMs = 500;
    for (int row = 0; row < m_sensors->size(); ++row) {
        const SensorModel::Entry &e = m_sensors->entry(row);
        if (!e.available || !(e.shown() || m_recordAll))
            continue;
        const int interval = pollIntervalMs(e);
        const qint64 waited = nowMs - e.lastPollMs;
        const double ratio = double(waited) / interval;
        if (ratio >= bestRatio) {
            best = row;
            bestRatio = ratio;
        } else {
            nextDueMs = std::min(nextDueMs, interval - waited);
        }
    }
    if (best >= 0)
        pollEntry(best);
    else
        m_idleTimer.start(int(std::max<qint64>(nextDueMs, 20)));
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
        const double t = m_clock.elapsed() / 1000.0;
        e.series->append(t, *value);
        m_recorder.record(row, t, *value);
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

// --- Recording -----------------------------------------------------------------

void ObdSession::setRecordAll(bool on)
{
    if (m_recordAll == on)
        return;
    m_recordAll = on;
    QSettings().setValue(QStringLiteral("record/all"), on);
    emit recordAllChanged();
    if (m_state == Running)
        pollNext();
}

void ObdSession::startRecording()
{
    QStringList columns;
    for (int row = 0; row < m_sensors->size(); ++row) {
        const SensorSeries *series = m_sensors->entry(row).series;
        columns << (series->unit().isEmpty() ? series->name()
                                             : QStringLiteral("%1 [%2]").arg(series->name(), series->unit()));
    }
    if (m_recorder.start(Recorder::defaultDirectory(), m_connectionName, columns, m_clockStart))
        appendLog(tr("Recording to %1").arg(QFileInfo(m_recorder.filePath()).fileName()));
    else
        appendLog(tr("! Cannot record: %1").arg(m_recorder.errorString()));
    emit recordingChanged();
    refreshRecordings();
}

void ObdSession::stopRecording()
{
    if (!m_recorder.isRecording())
        return;
    m_recorder.stop();
    emit recordingChanged();
    refreshRecordings();
}

void ObdSession::refreshRecordings()
{
    m_recorder.flush(); // so the current file shows its real size
    const QDir dir(Recorder::defaultDirectory());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("obd_*.csv")}, QDir::Files, QDir::Time);
    const QLocale locale;
    m_recordings.clear();
    for (const QFileInfo &file : files) {
        // obd_<yyyyMMdd>_<HHmmss>_<connection>.csv
        const QStringList parts = file.completeBaseName().split(QLatin1Char('_'));
        const QDateTime started = parts.size() >= 3
                                      ? QDateTime::fromString(parts[1] + parts[2], QStringLiteral("yyyyMMddHHmmss"))
                                      : file.lastModified();
        QString connection = parts.size() >= 4 ? parts.mid(3).join(QLatin1Char(' ')) : QString();
        const bool current = m_recorder.isRecording() && file.absoluteFilePath() == QFileInfo(m_recorder.filePath()).absoluteFilePath();
        QVariantMap item;
        item.insert(QStringLiteral("path"), file.absoluteFilePath());
        item.insert(QStringLiteral("fileName"), file.fileName());
        item.insert(QStringLiteral("title"), locale.toString(started.isValid() ? started : file.lastModified(),
                                                             QLocale::ShortFormat));
        item.insert(QStringLiteral("details"), QStringLiteral("%1 · %2").arg(connection.replace(QLatin1Char('-'), QLatin1Char(' ')),
                                                                             locale.formattedDataSize(file.size())));
        item.insert(QStringLiteral("current"), current);
        m_recordings << item;
    }
    emit recordingsChanged();
}

QString ObdSession::exportRecording(const QString &path, const QUrl &target)
{
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(m_recorder.filePath()).absoluteFilePath())
        m_recorder.flush();

    QFile source(path);
    if (!source.open(QIODevice::ReadOnly))
        return tr("Cannot read the recording: %1").arg(source.errorString());

    // On Android the save dialog returns a content:// URL, which QFile opens
    // through the system's content resolver.
    const QString destinationName = target.isLocalFile() ? target.toLocalFile() : target.toString();
    QFile destination(destinationName);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return tr("Cannot write the file: %1").arg(destination.errorString());

    qint64 copied = 0;
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(256 * 1024);
        if (chunk.isEmpty() || destination.write(chunk) != chunk.size())
            return tr("Writing the file failed: %1").arg(destination.errorString());
        copied += chunk.size();
    }
    destination.close();
    appendLog(tr("Exported %1 to %2").arg(QFileInfo(path).fileName(), destinationName));
    return tr("Saved %1").arg(QLocale().formattedDataSize(copied));
}

void ObdSession::deleteRecording(const QString &path)
{
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(m_recorder.filePath()).absoluteFilePath()
        && m_recorder.isRecording())
        return; // the current session's file stays while it is written
    // Only files in the recordings folder.
    if (QFileInfo(path).absolutePath() != QFileInfo(Recorder::defaultDirectory()).absoluteFilePath())
        return;
    QFile::remove(path);
    refreshRecordings();
}

// --- Fault codes ---------------------------------------------------------------

void ObdSession::resetDiagnostics()
{
    m_isCan = false;
    m_seriesByPid.clear();
    m_readiness.clear();
    m_healthChecks.clear();
    m_testsBusy = false;
    m_testsRead = false;
    m_testsStatus.clear();
    m_testResults.clear();
    emit healthChanged();
    emit testsChanged();
}

void ObdSession::evaluateHealth()
{
    if (m_sensors->size() == 0)
        return;
    const health::Lookup lookup = [this](int pid) -> const SensorSeries * { return m_seriesByPid.value(pid); };
    m_healthChecks.clear();
    for (const health::Check &check : health::evaluate(lookup, m_clock.elapsed() / 1000.0)) {
        m_healthChecks << QVariantMap{
            {QStringLiteral("title"), check.title},
            {QStringLiteral("status"), health::statusKey(check.status)},
            {QStringLiteral("value"), check.value},
            {QStringLiteral("detail"), check.detail},
        };
    }
    emit healthChanged();
}

void ObdSession::readTests()
{
    if (m_state != Running || m_testsBusy)
        return;
    if (!m_isCan) {
        m_testsRead = true;
        m_testsStatus = tr("Test results need a car on a CAN protocol (most cars from 2008 on). This car uses %1.").arg(m_protocol);
        emit testsChanged();
        return;
    }
    m_testsBusy = true;
    m_testResults.clear();
    m_testMids.clear();
    m_testsStatus = tr("Asking which tests the car has…");
    emit testsChanged();
    discoverTests(0x00);
}

void ObdSession::discoverTests(quint8 baseMid)
{
    const quint64 generation = m_generation;
    m_elm.send(QStringLiteral("06") + obd::hex2(baseMid), [this, generation, baseMid](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        const auto mask = elm::supportedMask(reply.lines, baseMid, 0x06);
        if (mask) {
            for (quint8 mid : obd::pidsFromMask(*mask, baseMid)) {
                if (!obd::isSupportMaskPid(mid))
                    m_testMids << mid;
            }
        }
        if (mask && (*mask & 1u) && baseMid < 0xE0) {
            discoverTests(quint8(baseMid + 0x20));
            return;
        }
        if (m_testMids.isEmpty()) {
            m_testsBusy = false;
            m_testsRead = true;
            m_testsStatus = tr("The car reports no test results (%1).")
                                .arg(reply.error.isEmpty() ? tr("no answer") : reply.error);
            emit testsChanged();
            return;
        }
        m_testsStatus = tr("Reading %n monitor(s)…", nullptr, int(m_testMids.size()));
        emit testsChanged();
        queryTests(m_testMids);
    }, 5000);
}

void ObdSession::queryTests(QList<quint8> mids)
{
    const quint64 generation = m_generation;
    const quint8 mid = mids.takeFirst();
    m_elm.send(QStringLiteral("06") + obd::hex2(mid), [this, generation, mid, mids](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        for (const obd::TestResult &r : obd::parseTests(reply.lines)) {
            if (r.mid != mid)
                continue;
            const QLocale c = QLocale::c();
            const auto format = [&](double v) {
                return c.toString(v, 'f', r.decimals) + (r.unit.isEmpty() ? QString() : QStringLiteral(" ") + r.unit);
            };
            // Where the value sits between the limits, for a bar (0..1).
            double position = -1;
            if (r.hasLimits && r.max > r.min)
                position = std::clamp((r.value - r.min) / (r.max - r.min), 0.0, 1.0);
            m_testResults << QVariantMap{
                {QStringLiteral("monitor"), obd::monitorName(r.mid)},
                {QStringLiteral("test"), obd::testName(r.mid, r.tid)},
                {QStringLiteral("value"), format(r.value)},
                {QStringLiteral("limits"), r.hasLimits ? tr("limits %1 … %2").arg(format(r.min), format(r.max)) : QString()},
                {QStringLiteral("status"), !r.hasLimits ? QStringLiteral("info") : r.passed ? QStringLiteral("pass") : QStringLiteral("fail")},
                {QStringLiteral("position"), position},
            };
        }
        if (!mids.isEmpty()) {
            queryTests(mids);
            return;
        }
        int failed = 0;
        for (const QVariant &v : std::as_const(m_testResults))
            failed += v.toMap().value(QStringLiteral("status")).toString() == QLatin1String("fail");
        m_testsBusy = false;
        m_testsRead = true;
        m_testsStatus = failed > 0 ? tr("%n test(s) outside their limits", nullptr, failed)
                                   : tr("%n test result(s), all within limits", nullptr, int(m_testResults.size()));
        appendLog(QStringLiteral("Mode 06: ") + m_testsStatus);
        emit testsChanged();
    }, 4000);
}

void ObdSession::resetCodes()
{
    m_dtcBusy = false;
    m_dtcRead = false;
    m_milOn = false;
    m_milCount = -1;
    m_dtcStatus.clear();
    m_dtcCodes.clear();
    emit dtcChanged();
}

void ObdSession::readCodes()
{
    if (m_state != Running || m_dtcBusy)
        return;
    m_dtcBusy = true;
    m_dtcCodes.clear();
    m_milOn = false;
    m_milCount = -1;
    m_dtcStatus = tr("Reading fault codes…");
    emit dtcChanged();

    // The requests queue up between the sensor polls.
    const quint64 generation = m_generation;
    m_elm.send(QStringLiteral("0101"), [this, generation](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        const auto payload = elm::mode01Payload(reply.lines, 0x01);
        if (payload && !payload->isEmpty()) {
            const auto a = quint8(payload->at(0));
            m_milOn = a & 0x80;
            m_milCount = a & 0x7F;
            m_readiness.clear();
            for (const obd::Monitor &monitor : obd::readiness(*payload)) {
                m_readiness << QVariantMap{
                    {QStringLiteral("name"), monitor.name},
                    {QStringLiteral("supported"), monitor.supported},
                    {QStringLiteral("complete"), monitor.complete},
                };
            }
        }
    }, 3000);

    struct Kind {
        const char *command;
        quint8 service;
        const char *name;
    };
    static const Kind kKinds[] = {
        {"03", 0x03, "stored"},
        {"07", 0x07, "pending"},
        {"0A", 0x0A, "permanent"},
    };
    for (const Kind &kind : kKinds) {
        const bool last = &kind == &kKinds[std::size(kKinds) - 1];
        m_elm.send(QString::fromLatin1(kind.command), [this, generation, kind, last](const Elm327::Reply &reply) {
            if (!isCurrent(generation))
                return;
            const QStringList codes = dtc::parse(reply.lines, kind.service);
            for (const QString &code : codes) {
                m_dtcCodes << QVariantMap{
                    {QStringLiteral("code"), code},
                    {QStringLiteral("description"), dtc::describe(code)},
                    {QStringLiteral("kind"), QString::fromLatin1(kind.name)},
                };
            }
            appendLog(QStringLiteral("%1 (%2): %3")
                          .arg(QString::fromLatin1(kind.name), QString::fromLatin1(kind.command),
                               codes.isEmpty() ? (reply.error.isEmpty() ? tr("none") : reply.error)
                                               : codes.join(QStringLiteral(", "))));
            if (last)
                finishCodes(std::exchange(m_dtcNote, QString()));
        }, 5000);
    }
}

void ObdSession::finishCodes(const QString &prefix)
{
    m_dtcBusy = false;
    m_dtcRead = true;
    QStringList parts;
    if (!prefix.isEmpty())
        parts << prefix;
    if (m_milCount >= 0)
        parts << (m_milOn ? tr("Check engine light ON") : tr("Check engine light off"));
    parts << (m_dtcCodes.isEmpty() ? tr("No fault codes") : tr("%n fault code(s)", nullptr, int(m_dtcCodes.size())));
    m_dtcStatus = parts.join(QStringLiteral(" · "));
    emit dtcChanged();
}

void ObdSession::clearCodes()
{
    if (m_state != Running || m_dtcBusy)
        return;
    m_dtcBusy = true;
    m_dtcStatus = tr("Clearing fault codes…");
    emit dtcChanged();
    appendLog(tr("Clearing fault codes (mode 04)"));

    const quint64 generation = m_generation;
    m_elm.send(QStringLiteral("04"), [this, generation](const Elm327::Reply &reply) {
        if (!isCurrent(generation))
            return;
        bool cleared = false;
        bool refused = false;
        for (const QString &line : reply.lines) {
            const QString c = elm::compact(line);
            cleared = cleared || c.startsWith(QLatin1String("44"));
            refused = refused || c.startsWith(QLatin1String("7F04"));
        }
        m_dtcBusy = false;
        if (cleared) {
            appendLog(tr("Fault codes cleared"));
            m_dtcNote = tr("Codes cleared");
            readCodes();
            return;
        }
        m_dtcStatus = refused
                          ? tr("The car refused to clear the codes. Switch the engine off, leave the ignition on and try again.")
                          : tr("Clearing failed: %1")
                                .arg(reply.error.isEmpty() ? reply.lines.join(QLatin1Char(' ')) : reply.error);
        appendLog(QStringLiteral("! ") + m_dtcStatus);
        emit dtcChanged();
    }, 10000);
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
