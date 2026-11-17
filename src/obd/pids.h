#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace obd {

// One SAE J1979 mode 01 PID that can be shown as a number over time.
struct PidInfo {
    quint8 pid;
    const char *name;   // UTF-8
    const char *unit;   // UTF-8
    double min;         // nominal range, used to scale charts
    double max;
    int decimals;       // digits shown after the decimal point
    int bytes;          // data bytes the decoder reads
    bool favorite;      // charted by default
    double (*decode)(const quint8 *data);
};

// The table entry for a PID, or nullptr if the PID is not a plain number
// (bit fields such as monitor status or fuel system status).
const PidInfo *findPid(quint8 pid);

// Decodes the data bytes that follow "41 <pid>" in a response.
std::optional<double> decode(quint8 pid, const QByteArray &data);

// How quickly a sensor's value changes, which sets how often it is read:
// engine speed many times a second, coolant temperature every few seconds.
enum class Pace { Fast, Medium, Slow };
Pace pace(quint8 pid);

// PIDs 00, 20, 40, ... answer with a bit mask of the 32 PIDs that follow them.
bool isSupportMaskPid(quint8 pid);
QList<quint8> pidsFromMask(quint32 mask, quint8 basePid);

// "0C" for 0x0C.
QString hex2(quint8 value);

} // namespace obd
