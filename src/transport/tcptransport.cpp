#include "transport/tcptransport.h"

TcpTransport::TcpTransport(const QString &host, quint16 port, QObject *parent)
    : Transport(parent)
    , m_socket(this)
    , m_host(host)
    , m_port(port)
{
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        emit opened();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] { emit dataReceived(m_socket.readAll()); });
    connect(&m_socket, &QTcpSocket::disconnected, this, &Transport::closed);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(m_socket.errorString());
    });
}

void TcpTransport::open()
{
    m_socket.connectToHost(m_host, m_port);
}

void TcpTransport::close()
{
    m_socket.abort();
}

void TcpTransport::write(const QByteArray &data)
{
    m_socket.write(data);
}
