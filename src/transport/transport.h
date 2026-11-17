#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// A byte pipe to an ELM327 adapter: Bluetooth, BLE, Wi-Fi or the simulator.
class Transport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void open() = 0;
    virtual void close() = 0;
    virtual void write(const QByteArray &data) = 0;

signals:
    void opened();
    void closed();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &message);
};
