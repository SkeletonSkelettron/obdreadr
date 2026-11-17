#pragma once

#include <QString>
#include <QStringList>

// Diagnostic trouble codes (SAE J2012) from modes 03, 07 and 0A.
namespace dtc {

// Two data bytes to a code: 01 33 -> "P0133".
QString fromBytes(quint8 a, quint8 b);

// The codes in an answer to `service` (0x03 stored, 0x07 pending,
// 0x0A permanent), without duplicates. Handles one or more ECUs, CAN
// answers (with a count byte, possibly multi-frame) and the older
// protocols (three codes per line, padded with zeros).
QStringList parse(const QStringList &lines, quint8 service);

// A short English description, or the code's category when it is not
// in the table.
QString describe(const QString &code);

} // namespace dtc
