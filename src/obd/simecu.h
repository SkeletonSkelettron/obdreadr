#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

// A pretend ELM327 adapter plugged into a pretend car with two ECUs on CAN:
// the engine (7E8), which answers nearly everything, and the transmission
// (7E9), which also reports vehicle speed. It answers the same text commands
// a real adapter does, with or without headers, including requests for up to
// six PIDs at once, so the demo mode and the unit tests run the real
// protocol code.
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
    // An OBD request, with or without a last digit for the number of
    // answers to wait for ("010C1"), which `reply` then keeps to.
    QString obdCommand(const QString &request, double t);
    std::optional<QByteArray> pidData(quint8 pid, double t) const;
    struct Message {
        quint16 sender; // CAN identifier
        QByteArray payload;
    };
    QString hex(const QByteArray &bytes) const;
    // The messages as the adapter prints them: without headers one line per
    // message, or ISO-TP frames with a length line when it does not fit in
    // one; with headers every frame with its sender and PCI byte, the ECUs'
    // frames interleaved.
    QString reply(const QList<Message> &messages) const;
    QString canAnswer(const QByteArray &payload) const;
    QString mode01(const QList<quint8> &pids, double t) const;
    QString codesAnswer(quint8 service, const QList<quint16> &codes) const;
    // Mode 02: the freeze frame stored with P0133, gone once codes are cleared.
    QString freezeFrameAnswer(quint8 pid, quint8 frame) const;

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
    QString m_header; // AT SH: the ECU addressed, empty for all
    int m_maxAnswers = 0; // of the request being answered, 0 for all
};
