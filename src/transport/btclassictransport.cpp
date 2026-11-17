#include "transport/btclassictransport.h"

#include <QBluetoothServiceInfo>
#include <QBluetoothSocket>
#include <QBluetoothUuid>

BtClassicTransport::BtClassicTransport(const QBluetoothAddress &address, QObject *parent)
    : Transport(parent)
    , m_address(address)
{
}

void BtClassicTransport::open()
{
    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);
    connect(m_socket, &QBluetoothSocket::connected, this, &Transport::opened);
    connect(m_socket, &QBluetoothSocket::disconnected, this, &Transport::closed);
    connect(m_socket, &QBluetoothSocket::readyRead, this, [this] {
        if (m_socket)
            emit dataReceived(m_socket->readAll());
    });
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, [this](QBluetoothSocket::SocketError) {
        emit errorOccurred(m_socket ? m_socket->errorString() : tr("Bluetooth error"));
    });
    m_socket->connectToService(m_address, QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort));
}

void BtClassicTransport::close()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
}

void BtClassicTransport::write(const QByteArray &data)
{
    if (m_socket)
        m_socket->write(data);
}
