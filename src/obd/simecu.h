#pragma once

#include <QByteArray>
#include <QSet>
#include <QString>

#include <optional>

// A pretend ELM327 adapter plugged into a pretend car. It answers the same
// text commands a real adapter does, so the demo mode and the unit tests run
// the real protocol code.
class SimEcu
{
public:
    SimEcu();

    // Handles one command (without the trailing '\r') at time t seconds and
    // returns everything the adapter would send, ending with the '>' prompt.
    QByteArray handle(const QByteArray &command, double t);

    static QString vin();

private:
    QString atCommand(const QString &cmd);
    QString obdCommand(const QString &cmd, double t);
    std::optional<QByteArray> pidData(quint8 pid, double t) const;
    QString hex(const QByteArray &bytes) const;

    QSet<quint8> m_supported;
    bool m_echo = true;
    bool m_spaces = true;
    bool m_headers = false;
    bool m_searched = false;
};
