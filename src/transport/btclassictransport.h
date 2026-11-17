#pragma once

#include "transport/transport.h"

#include <QBluetoothAddress>
#include <QPointer>

class QBluetoothSocket;

// Classic Bluetooth ELM327 adapters expose a serial port (SPP/RFCOMM).
class BtClassicTransport : public Transport
{
    Q_OBJECT

public:
    explicit BtClassicTransport(const QBluetoothAddress &address, QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &data) override;

private:
    QBluetoothAddress m_address;
    QPointer<QBluetoothSocket> m_socket;
};
