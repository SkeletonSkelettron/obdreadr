#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTimer>

// Writes every reading of a session to a CSV file: a time column, a
// timestamp column and one column per sensor, one row per reading. The
// file is flushed every few seconds, so little is lost if the app dies.
class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    // Where the app keeps its recordings.
    static QString defaultDirectory();

    // Starts a new file in `directory`. `clockStart` is the wall time of
    // t = 0 in record(). Old recordings beyond the newest `keep` are removed.
    bool start(const QString &directory, const QString &label, const QStringList &columns,
               const QDateTime &clockStart, int keep = 50);
    void stop();
    void flush();

    bool isRecording() const { return m_file.isOpen(); }
    QString filePath() const { return m_file.fileName(); }
    QString errorString() const { return m_error; }

    void record(int column, double seconds, double value);

private:
    QFile m_file;
    QDateTime m_clockStart;
    int m_columns = 0;
    QByteArray m_buffer;
    QTimer m_flushTimer;
    QString m_error;
};
