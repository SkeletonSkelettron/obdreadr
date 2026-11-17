#pragma once

#include <QByteArray>
#include <QList>
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
    // One line, or ISO-TP frames when the answer does not fit in one.
    QString canAnswer(const QByteArray &payload) const;
    QString codesAnswer(quint8 service, const QList<quint16> &codes) const;

    QSet<quint8> m_supported;
    QList<quint16> m_stored = {0x0133, 0x0420};
    QList<quint16> m_pending = {0x0171};
    QList<quint16> m_permanent = {0x0420};
    // Readiness: monitors that have not completed (byte D of PID 01).
    quint8 m_incomplete = 0x04; // the evaporative system test
    QString testsAnswer(quint8 mid) const;
    bool m_echo = true;
    bool m_spaces = true;
    bool m_headers = false;
    bool m_searched = false;
};
