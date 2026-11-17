#pragma once

#include "transport/transport.h"

#include <QTcpSocket>

// Wi-Fi ELM327 adapters listen on a TCP port, usually 192.168.0.10:35000.
class TcpTransport : public Transport
{
    Q_OBJECT

public:
    TcpTransport(const QString &host, quint16 port, QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &data) override;

private:
    QTcpSocket m_socket;
    QString m_host;
    quint16 m_port;
};
