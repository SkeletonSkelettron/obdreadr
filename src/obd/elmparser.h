#pragma once

#include <QByteArray>
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

// The supported-PID mask answered to PID 00, 20, 40, ...; masks from
// several ECUs are OR-ed together.
std::optional<quint32> supportedMask(const QStringList &lines, quint8 basePid);

// The vehicle identification number from a "09 02" response (CAN multi-frame
// or older one-line-per-message formats), or an empty string.
QString parseVin(const QStringList &lines);

// Battery voltage from an "AT RV" response such as "12.6V".
std::optional<double> parseVoltage(const QStringList &lines);

// Human readable name for an "AT DPN" answer such as "A6".
QString protocolName(const QString &dpn);

} // namespace elm
