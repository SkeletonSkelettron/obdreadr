#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Parsing of ELM327 text responses. Everything here is pure and unit-tested.
namespace elm {

// Removes spaces and upper-cases: "41 0c 1a f8" -> "410C1AF8".
QString compact(const QString &line);

bool isHex(const QString &text);

// Splits the text an adapter sent before its '>' prompt into trimmed,
// upper-case, non-empty lines.
QStringList splitLines(const QByteArray &raw);

// Drops the echo of the command and progress lines such as "SEARCHING...".
QStringList cleanLines(const QStringList &lines, const QString &command);

// The adapter's error message ("NO DATA", "UNABLE TO CONNECT", "?", ...),
// or an empty string when the lines contain none.
QString errorIn(const QStringList &lines);

// The data bytes after "41 <pid>" from the first ECU that answered.
std::optional<QByteArray> mode01Payload(const QStringList &lines, quint8 pid);

// The data bytes of the answer to any request `command` (hex, e.g. 221105):
// after the reply service (first byte + 0x40) and the rest of the request.
std::optional<QByteArray> customPayload(const QStringList &lines, const QString &command);

// The data bytes after "42 <pid> <frame>" in a freeze frame answer (mode 02).
std::optional<QByteArray> mode02Payload(const QStringList &lines, quint8 pid, quint8 frame = 0);

// The supported-PID mask answered to PID 00, 20, 40, ... of `service`
// (01 for sensors, 06 for test results); masks from several ECUs are OR-ed.
std::optional<quint32> supportedMask(const QStringList &lines, quint8 basePid, quint8 service = 0x01);

// The answer as one byte string per message. A multi-frame CAN answer
// (a length line "00A", then frames "0:", "1:", ...) becomes one message,
// cut to its length; every other hex line is a message of its own.
QList<QByteArray> messages(const QStringList &lines);

// With headers on (AT H1) a CAN adapter prints each frame with its sender
// and its ISO-TP PCI byte: "7E8 06 41 0C 1A F8", or "7E8 10 0A 41 ..." and
// "7E8 21 ..." for a longer answer, with several ECUs' frames possibly
// interleaved. This turns them into one hex line per message, ECU by ECU,
// which the other functions here read like header-less answers.
// `headerChars` is 3 for 11-bit and 8 for 29-bit identifiers; lines that do
// not look like frames (errors, an adapter ignoring AT H1) pass unchanged.
QStringList stripCanHeaders(const QStringList &lines, int headerChars);

// The data of each PID in an answer to a mode 01 request for several PIDs
// ("01 0C 0D 10"): every message "41 0C a b 0D c 10 d e" is split using
// `lengths` (PID -> data bytes, learned from single requests). A message
// that does not split exactly into known PIDs is ignored as a whole; when
// several ECUs answer a PID, the first one counts.
QHash<quint8, QByteArray> mode01Values(const QStringList &lines, const QHash<quint8, int> &lengths);

// How many ECUs answered a mode 01 request for `pid`: its messages (one
// per ECU) starting "41 <pid>".
int mode01Responders(const QStringList &lines, quint8 pid);

// Bytes a CAN frame carries of a message; a longer answer takes several
// frames, which the adapter must ask for (ISO-TP flow control).
constexpr int kSingleFrameBytes = 7;

// Size of an ECU's answer to a mode 01 request for PIDs with these data
// lengths: "41", then each PID and its data.
int mode01AnswerBytes(const QList<int> &dataLengths);

// A recent ELM327 takes the number of answers to wait for as one hex digit
// after the request ("01 0C 1") and returns as soon as they are in, instead
// of waiting for more until its timeout. Whether the answer to
// "0100" + "1" shows an adapter that understood it: exactly one answer to
// PID 00 ("?" from an older chip, or a second PID 10 when the digit was
// read as part of the request, are no).
bool takesResponseCount(const QStringList &lines);

// The vehicle identification number from a "09 02" response (CAN multi-frame
// or older one-line-per-message formats), or an empty string.
QString parseVin(const QStringList &lines);

// Battery voltage from an "AT RV" response such as "12.6V".
std::optional<double> parseVoltage(const QStringList &lines);

// Human readable name for an "AT DPN" answer such as "A6".
QString protocolName(const QString &dpn);

// Whether an "AT DPN" answer is one of the CAN protocols (6 to C).
bool isCanProtocol(const QString &dpn);

// Hex digits of the CAN identifier an adapter prints with headers on: 3 for
// the 11-bit OBD protocols (6, 8), 8 for the 29-bit ones (7, 9), else 0.
int canHeaderLength(const QString &dpn);

} // namespace elm
