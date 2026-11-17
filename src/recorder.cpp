#include "recorder.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

constexpr int kFlushMs = 2000;
constexpr qsizetype kFlushBytes = 64 * 1024;

QByteArray quoted(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return '"' + escaped.toUtf8() + '"';
}

} // namespace

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    m_flushTimer.setInterval(kFlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &Recorder::flush);
}

Recorder::~Recorder()
{
    stop();
}

QString Recorder::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recordings");
}

bool Recorder::start(const QString &directory, const QString &label, const QStringList &columns,
                     const QDateTime &clockStart, int keep)
{
    stop();
    m_error.clear();
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral("."))) {
        m_error = tr("Cannot create %1").arg(directory);
        return false;
    }

    // Keep the newest recordings only.
    const QFileInfoList old = dir.entryInfoList({QStringLiteral("obd_*.csv")}, QDir::Files, QDir::Time);
    for (qsizetype i = keep - 1; i >= 0 && i < old.size(); ++i)
        QFile::remove(old[i].absoluteFilePath());

    QString safeLabel = label;
    safeLabel.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.-]+")), QStringLiteral("-"));
    const QString base = QStringLiteral("obd_%1_%2")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")),
                                  safeLabel.left(40));
    QString path = dir.filePath(base + QStringLiteral(".csv"));
    for (int n = 2; QFile::exists(path); ++n)
        path = dir.filePath(base + QStringLiteral("-%1.csv").arg(n));
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = m_file.errorString();
        return false;
    }

    m_clockStart = clockStart;
    m_columns = int(columns.size());
    m_buffer = "time_s,timestamp";
    for (const QString &column : columns)
        m_buffer += ',' + quoted(column);
    m_buffer += '\n';
    flush();
    m_flushTimer.start();
    return true;
}

void Recorder::stop()
{
    if (!m_file.isOpen())
        return;
    m_flushTimer.stop();
    flush();
    m_file.close();
}

void Recorder::flush()
{
    if (!m_file.isOpen() || m_buffer.isEmpty())
        return;
    m_file.write(m_buffer);
    m_file.flush();
    m_buffer.clear();
}

void Recorder::record(int column, double seconds, double value)
{
    if (!m_file.isOpen() || column < 0 || column >= m_columns)
        return;
    m_buffer += QByteArray::number(seconds, 'f', 3);
    m_buffer += ',';
    m_buffer += m_clockStart.addMSecs(qint64(seconds * 1000))
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                    .toLatin1();
    for (int i = 0; i < m_columns; ++i) {
        m_buffer += ',';
        if (i == column)
            m_buffer += QByteArray::number(value, 'g', 10);
    }
    m_buffer += '\n';
    if (m_buffer.size() >= kFlushBytes)
        flush();
}
