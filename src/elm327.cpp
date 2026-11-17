#include "elm327.h"

#include "obd/elmparser.h"
#include "transport/transport.h"

namespace {
constexpr int kDrainMs = 1500;
constexpr qsizetype kMaxBuffer = 16384;
} // namespace

Elm327::Elm327(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Elm327::onTimer);
}

void Elm327::setTransport(Transport *transport)
{
    reset();
    disconnect(m_dataConnection);
    m_transport = transport;
    if (transport)
        m_dataConnection = connect(transport, &Transport::dataReceived, this, &Elm327::onData);
}

void Elm327::send(const QString &command, Callback callback, int timeoutMs)
{
    m_queue.push_back({command, std::move(callback), timeoutMs});
    startNext();
}

void Elm327::reset()
{
    m_queue.clear();
    m_current.reset();
    m_buffer.clear();
    m_draining = false;
    m_timer.stop();
}

void Elm327::startNext()
{
    if (m_current || m_draining || m_queue.empty())
        return;
    m_current = std::move(m_queue.front());
    m_queue.pop_front();
    m_buffer.clear();

    if (!m_transport) {
        // Keep callbacks asynchronous, as they are with a live adapter.
        QTimer::singleShot(0, this, [this, command = m_current->command] {
            if (m_current && m_current->command == command)
                finish({}, QStringLiteral("NOT CONNECTED"));
        });
        return;
    }
    if (m_verbose)
        emit logLine(QStringLiteral("> ") + m_current->command);
    m_transport->write(m_current->command.toLatin1() + '\r');
    m_timer.start(m_current->timeoutMs);
}

void Elm327::onData(const QByteArray &data)
{
    m_buffer += data;
    const qsizetype prompt = m_buffer.indexOf('>');
    if (prompt < 0) {
        if (m_buffer.size() > kMaxBuffer)
            m_buffer = m_buffer.right(kMaxBuffer / 2);
        return;
    }
    const QByteArray raw = m_buffer.left(prompt);
    m_buffer.remove(0, prompt + 1);

    if (m_draining) {
        m_draining = false;
        m_timer.stop();
        m_buffer.clear();
        startNext();
        return;
    }
    if (!m_current)
        return; // unsolicited text

    m_timer.stop();
    const QStringList lines = elm::cleanLines(elm::splitLines(raw), m_current->command);
    finish(lines, elm::errorIn(lines));
}

void Elm327::onTimer()
{
    if (m_draining) {
        m_draining = false;
        m_buffer.clear();
        startNext();
        return;
    }
    if (!m_current)
        return;
    m_draining = true;
    m_timer.start(kDrainMs);
    finish({}, QStringLiteral("TIMEOUT"));
}

void Elm327::finish(const QStringList &lines, const QString &error)
{
    const Request request = std::move(*m_current);
    m_current.reset();
    if (m_verbose || !error.isEmpty()) {
        const QString text = lines.isEmpty() ? error : lines.join(QStringLiteral(" | "));
        emit logLine(QStringLiteral("< ") + (m_verbose ? QString() : request.command + QStringLiteral(": ")) + text);
    }
    if (request.callback)
        request.callback({lines, error});
    startNext();
}
