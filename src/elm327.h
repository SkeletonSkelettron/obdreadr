#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <functional>
#include <optional>

class Transport;

// Sends ELM327 commands one at a time and collects each answer up to the
// '>' prompt.
class Elm327 : public QObject
{
    Q_OBJECT

public:
    struct Reply {
        QStringList lines;  // cleaned answer lines
        QString error;      // adapter error, "TIMEOUT", or empty
    };
    using Callback = std::function<void(const Reply &)>;

    explicit Elm327(QObject *parent = nullptr);

    void setTransport(Transport *transport);
    void send(const QString &command, Callback callback = {}, int timeoutMs = 2000);

    // Forgets queued and pending commands without calling their callbacks.
    void reset();

    // When false, only failed commands are logged.
    void setVerbose(bool verbose) { m_verbose = verbose; }

signals:
    void logLine(const QString &line);

private:
    struct Request {
        QString command;
        Callback callback;
        int timeoutMs;
    };

    void startNext();
    void onData(const QByteArray &data);
    void onTimer();
    void finish(const QStringList &lines, const QString &error);

    QPointer<Transport> m_transport;
    QMetaObject::Connection m_dataConnection;
    std::deque<Request> m_queue;
    std::optional<Request> m_current;
    QByteArray m_buffer;
    QTimer m_timer;
    // After a timeout the late answer may still come; wait for its prompt
    // before sending anything else so answers stay paired with commands.
    bool m_draining = false;
    bool m_verbose = true;
};
