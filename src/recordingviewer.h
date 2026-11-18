#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class SensorSeries;

// Opens a recording for the recording page: one series per sensor with
// readings, for the charts, and a summary of the drive.
class RecordingViewer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY loadedChanged)
    Q_PROPERTY(QString title READ title NOTIFY loadedChanged)
    Q_PROPERTY(QList<QObject *> series READ series NOTIFY loadedChanged)
    Q_PROPERTY(double startTime READ startTime NOTIFY loadedChanged)
    Q_PROPERTY(double endTime READ endTime NOTIFY loadedChanged)
    Q_PROPERTY(QVariantList summary READ summary NOTIFY loadedChanged)

public:
    explicit RecordingViewer(QObject *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
    QString title() const { return m_title; }
    QList<QObject *> series() const { return m_series; }
    double startTime() const { return m_startTime; }
    double endTime() const { return m_endTime; }
    // [{label, value}]
    QVariantList summary() const { return m_summary; }

signals:
    void pathChanged();
    void loadedChanged();

private:
    void load();

    QString m_path;
    bool m_loaded = false;
    QString m_error;
    QString m_title;
    QList<QObject *> m_series;
    double m_startTime = 0;
    double m_endTime = 0;
    QVariantList m_summary;
};
