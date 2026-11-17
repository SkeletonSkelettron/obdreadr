#include "transport/simtransport.h"

#include <QRandomGenerator>
#include <QTimer>

SimTransport::SimTransport(QObject *parent)
    : Transport(parent)
{
}

void SimTransport::open()
{
    m_clock.start();
    QTimer::singleShot(400, this, [this] {
        m_open = true;
        emit opened();
    });
}

void SimTransport::close()
{
    m_open = false;
    m_input.clear();
}

void SimTransport::write(const QByteArray &data)
{
    if (!m_open)
        return;
    m_input += data;
    int end;
    while ((end = m_input.indexOf('\r')) >= 0) {
        const QByteArray command = m_input.left(end);
        m_input.remove(0, end + 1);
        const QByteArray reply = m_ecu.handle(command, m_clock.elapsed() / 1000.0);
        const bool reset = command.trimmed().toUpper() == "ATZ";
        const int delay = reset ? 800 : 25 + int(QRandomGenerator::global()->bounded(30));
        QTimer::singleShot(delay, this, [this, reply] {
            if (m_open)
                emit dataReceived(reply);
        });
    }
}
