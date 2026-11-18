#include "recordingviewer.h"

#include "obd/pids.h"
#include "recording.h"
#include "sensorseries.h"

#include <QFile>
#include <QLocale>
#include <QSettings>

#include <algorithm>
#include <limits>

RecordingViewer::RecordingViewer(QObject *parent)
    : QObject(parent)
{
}

void RecordingViewer::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    emit pathChanged();
    load();
}

void RecordingViewer::load()
{
    qDeleteAll(m_series);
    m_series.clear();
    m_summary.clear();
    m_loaded = false;
    m_error.clear();
    m_title.clear();

    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = tr("Cannot open the recording: %1").arg(file.errorString());
        emit loadedChanged();
        return;
    }
    QString error;
    const std::optional<recording::Data> data = recording::parse(file.readAll(), &error);
    if (!data) {
        m_error = error;
        emit loadedChanged();
        return;
    }

    m_startTime = data->firstTime;
    m_endTime = data->lastTime;
    if (data->start.isValid())
        m_title = QLocale().toString(data->start.addMSecs(qint64(data->firstTime * 1000)), QLocale::ShortFormat);

    int index = 0;
    for (const recording::Column &column : data->columns) {
        if (column.points.empty())
            continue;
        const obd::PidInfo *info = column.pid >= 0 ? obd::findPid(quint8(column.pid)) : nullptr;
        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (const recording::Point &p : column.points) {
            lo = std::min(lo, p.v);
            hi = std::max(hi, p.v);
        }
        auto *series = new SensorSeries(column.name, column.unit, info ? info->decimals : (column.pid == -1 ? 2 : 1),
                                        info ? info->min : lo, info ? info->max : hi,
                                        SensorSeries::paletteColor(index++), nullptr, this);
        series->setRetention(std::numeric_limits<double>::max());
        for (const recording::Point &p : column.points)
            series->append(p.t, p.v);
        m_series << series;
    }

    // The summary uses the fuel settings of the Fuel tab.
    const QSettings settings;
    recording::FuelSettings fuel;
    fuel.kind = fuel::Kind(std::clamp(settings.value(QStringLiteral("fuel/type"), 0).toInt(), 0, 2));
    fuel.engineLiters = settings.value(QStringLiteral("fuel/engineLiters"), 1.6).toDouble();
    fuel.correction = settings.value(QStringLiteral("fuel/correction"), 100).toInt() / 100.0;
    fuel.price = settings.value(QStringLiteral("fuel/price"), 0.0).toDouble();
    for (const auto &item : recording::summarize(*data, fuel))
        m_summary << QVariantMap{{QStringLiteral("label"), item.first}, {QStringLiteral("value"), item.second}};

    m_loaded = true;
    emit loadedChanged();
}
