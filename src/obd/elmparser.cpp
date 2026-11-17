#include "obd/elmparser.h"

#include "obd/pids.h"

#include <iterator>
#include <utility>

namespace elm {

namespace {

// "0:410C1AF8" -> "410C1AF8". ISO-TP frames are numbered with one hex digit.
QString stripFrameIndex(const QString &compacted)
{
    const int colon = compacted.indexOf(QLatin1Char(':'));
    if (colon > 0 && colon <= 2)
        return compacted.mid(colon + 1);
    return compacted;
}

} // namespace

QString compact(const QString &line)
{
    QString out;
    out.reserve(line.size());
    for (const QChar c : line) {
        if (!c.isSpace())
            out += c.toUpper();
    }
    return out;
}

bool isHex(const QString &text)
{
    if (text.isEmpty() || text.size() % 2 != 0)
        return false;
    for (const QChar c : text) {
        const char ch = c.toLatin1();
        const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
        if (!hex)
            return false;
    }
    return true;
}

QStringList splitLines(const QByteArray &raw)
{
    QString text = QString::fromLatin1(raw);
    text.remove(QChar(0));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    QStringList lines;
    const QStringList parts = text.split(QLatin1Char('\r'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString line = part.trimmed().toUpper();
        if (!line.isEmpty())
            lines << line;
    }
    return lines;
}

QStringList cleanLines(const QStringList &lines, const QString &command)
{
    const QString echo = compact(command);
    QStringList out;
    for (const QString &line : lines) {
        const QString c = compact(line);
        if (c == echo)
            continue;
        if (c.startsWith(QLatin1String("SEARCHING")))
            continue;
        if (c.startsWith(QLatin1String("BUSINIT")) && !c.contains(QLatin1String("ERROR")))
            continue;
        out << line;
    }
    return out;
}

QString errorIn(const QStringList &lines)
{
    static const struct {
        const char *compacted;
        const char *message;
    } kErrors[] = {
        {"NODATA", "NO DATA"},
        {"UNABLETOCONNECT", "UNABLE TO CONNECT"},
        {"CANERROR", "CAN ERROR"},
        {"BUSERROR", "BUS ERROR"},
        {"BUSBUSY", "BUS BUSY"},
        {"FBERROR", "FB ERROR"},
        {"DATAERROR", "DATA ERROR"},
        {"BUFFERFULL", "BUFFER FULL"},
        {"RXERROR", "RX ERROR"},
        {"STOPPED", "STOPPED"},
        {"LVRESET", "LV RESET"},
        {"ACTALERT", "ACT ALERT"},
    };
    for (const QString &line : lines) {
        const QString c = compact(line);
        if (c == QLatin1String("?"))
            return QStringLiteral("?");
        for (const auto &error : kErrors) {
            if (c.contains(QLatin1String(error.compacted)))
                return QString::fromLatin1(error.message);
        }
        if (c.startsWith(QLatin1String("BUSINIT")) && c.contains(QLatin1String("ERROR")))
            return QStringLiteral("BUS INIT ERROR");
        if (c.startsWith(QLatin1String("ERR")) && c.size() <= 5)
            return c;
    }
    return {};
}

std::optional<QByteArray> mode01Payload(const QStringList &lines, quint8 pid)
{
    const QString prefix = QStringLiteral("41") + obd::hex2(pid);
    for (const QString &line : lines) {
        const QString c = stripFrameIndex(compact(line));
        if (c.size() > prefix.size() && c.startsWith(prefix) && isHex(c))
            return QByteArray::fromHex(c.mid(prefix.size()).toLatin1());
    }
    return std::nullopt;
}

std::optional<quint32> supportedMask(const QStringList &lines, quint8 basePid, quint8 service)
{
    const QString prefix = obd::hex2(quint8(service + 0x40)) + obd::hex2(basePid);
    std::optional<quint32> mask;
    for (const QString &line : lines) {
        const QString c = stripFrameIndex(compact(line));
        if (c.size() < prefix.size() + 8 || !c.startsWith(prefix) || !isHex(c))
            continue;
        bool ok = false;
        const quint32 bits = c.mid(prefix.size(), 8).toUInt(&ok, 16);
        if (ok)
            mask = mask.value_or(0) | bits;
    }
    return mask;
}

QList<QByteArray> messages(const QStringList &lines)
{
    struct Message {
        QByteArray bytes;
        int length = -1; // from the ISO-TP length line, when there is one
    };
    QList<Message> found;
    int pendingLength = -1;
    for (const QString &line : lines) {
        const QString c = compact(line);
        const int colon = c.indexOf(QLatin1Char(':'));
        if (colon > 0 && colon <= 2) {
            const QString hex = c.mid(colon + 1);
            if (!isHex(hex))
                continue;
            if (c.left(colon) == QLatin1String("0") || found.isEmpty()) {
                found << Message{{}, pendingLength};
                pendingLength = -1;
            }
            found.last().bytes += QByteArray::fromHex(hex.toLatin1());
            continue;
        }
        if (c.size() == 3) {
            bool ok = false;
            const int length = c.toInt(&ok, 16);
            if (ok) {
                pendingLength = length;
                continue;
            }
        }
        if (isHex(c))
            found << Message{QByteArray::fromHex(c.toLatin1()), -1};
    }
    QList<QByteArray> result;
    for (Message &m : found) {
        if (m.length >= 0 && m.bytes.size() > m.length)
            m.bytes.truncate(m.length);
        result << m.bytes;
    }
    return result;
}

QString parseVin(const QStringList &lines)
{
    bool framed = false;
    for (const QString &line : lines) {
        const QString c = compact(line);
        const int colon = c.indexOf(QLatin1Char(':'));
        if (colon > 0 && colon <= 2)
            framed = true;
    }

    QByteArray bytes;
    if (framed) {
        // CAN: "014" (length), then "0:490201...", "1:...", "2:...".
        for (const QString &line : lines) {
            const QString c = compact(line);
            const int colon = c.indexOf(QLatin1Char(':'));
            if (colon <= 0 || colon > 2)
                continue;
            const QString hex = c.mid(colon + 1);
            if (isHex(hex))
                bytes += QByteArray::fromHex(hex.toLatin1());
        }
        const int start = bytes.indexOf(QByteArray("\x49\x02", 2));
        if (start < 0)
            return {};
        bytes = bytes.mid(start + 3); // 49 02 <count>
    } else {
        // Older protocols: one "49 02 <n> xx xx xx xx" line per message.
        for (const QString &line : lines) {
            const QString c = compact(line);
            if (c.startsWith(QLatin1String("4902")) && c.size() > 6 && isHex(c))
                bytes += QByteArray::fromHex(c.mid(6).toLatin1());
        }
    }

    QString vin;
    for (const char ch : std::as_const(bytes)) {
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z'))
            vin += QLatin1Char(ch);
    }
    if (vin.size() < 17)
        return {};
    return vin.right(17);
}

std::optional<double> parseVoltage(const QStringList &lines)
{
    for (const QString &line : lines) {
        QString c = compact(line);
        if (c.endsWith(QLatin1Char('V')))
            c.chop(1);
        bool ok = false;
        const double volts = c.toDouble(&ok);
        if (ok && volts > 0 && volts < 40)
            return volts;
    }
    return std::nullopt;
}

QString protocolName(const QString &dpn)
{
    QString code = compact(dpn);
    if (code.size() == 2 && code.startsWith(QLatin1Char('A')))
        code = code.mid(1);
    static const char *kNames[] = {
        "Automatic",
        "SAE J1850 PWM",
        "SAE J1850 VPW",
        "ISO 9141-2",
        "ISO 14230-4 KWP (5 baud init)",
        "ISO 14230-4 KWP (fast init)",
        "CAN 11 bit, 500 kbaud",
        "CAN 29 bit, 500 kbaud",
        "CAN 11 bit, 250 kbaud",
        "CAN 29 bit, 250 kbaud",
        "SAE J1939 CAN",
        "User1 CAN",
        "User2 CAN",
    };
    bool ok = false;
    const int index = code.toInt(&ok, 16);
    if (code.size() == 1 && ok && index >= 0 && index < int(std::size(kNames)))
        return QString::fromLatin1(kNames[index]);
    return dpn;
}

bool isCanProtocol(const QString &dpn)
{
    QString code = compact(dpn);
    if (code.size() == 2 && code.startsWith(QLatin1Char('A')))
        code = code.mid(1);
    bool ok = false;
    const int index = code.toInt(&ok, 16);
    return code.size() == 1 && ok && index >= 6 && index <= 0xC;
}

} // namespace elm
