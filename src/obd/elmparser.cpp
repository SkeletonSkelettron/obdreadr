#include "obd/elmparser.h"

#include "obd/pids.h"

#include <algorithm>
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

// Hex digits only, of any count (an 11-bit CAN header makes it odd).
bool hexDigits(const QString &text)
{
    return !text.isEmpty() && std::all_of(text.begin(), text.end(), [](QChar c) {
        const char ch = c.toLatin1();
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F');
    });
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

std::optional<QByteArray> customPayload(const QStringList &lines, const QString &command)
{
    QByteArray prefix = QByteArray::fromHex(compact(command).toLatin1());
    if (prefix.isEmpty())
        return std::nullopt;
    prefix[0] = char(quint8(prefix.at(0)) + 0x40);
    for (const QByteArray &message : messages(lines)) {
        if (message.size() > prefix.size() && message.startsWith(prefix))
            return message.mid(prefix.size());
    }
    return std::nullopt;
}

std::optional<QByteArray> mode02Payload(const QStringList &lines, quint8 pid, quint8 frame)
{
    const QString prefix = QStringLiteral("42") + obd::hex2(pid) + obd::hex2(frame);
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

QStringList stripCanHeaders(const QStringList &lines, int headerChars)
{
    if (headerChars <= 0)
        return lines;
    struct Message {
        QString sender;
        QByteArray bytes;
        int length = 0;
        int nextIndex = 1; // of the next consecutive frame
        bool open = false; // waiting for more frames
        bool broken = false;
    };
    QStringList passed;
    QList<Message> found;
    const auto openMessage = [&found](const QString &sender) -> Message * {
        for (qsizetype i = found.size() - 1; i >= 0; --i) {
            if (found[i].sender == sender && found[i].open)
                return &found[i];
        }
        return nullptr;
    };

    for (const QString &line : lines) {
        const QString c = compact(line);
        const qsizetype dataChars = c.size() - headerChars;
        const bool framed = hexDigits(c) && dataChars >= 2 && dataChars % 2 == 0
                            && (headerChars != 8 || c.startsWith(QLatin1String("18DA")));
        if (!framed) {
            passed << line;
            continue;
        }
        const QString sender = c.left(headerChars);
        const QByteArray frame = QByteArray::fromHex(c.mid(headerChars).toLatin1());
        const auto pci = quint8(frame.at(0));
        switch (pci >> 4) {
        case 0x0: { // single frame
            const int length = pci & 0x0F;
            if (length == 0 || frame.size() < 1 + length)
                break;
            found << Message{sender, frame.mid(1, length), length, 1, false, false};
            break;
        }
        case 0x1: { // first frame
            if (frame.size() < 2)
                break;
            const int length = (pci & 0x0F) << 8 | quint8(frame.at(1));
            found << Message{sender, frame.mid(2), length, 1, true, false};
            break;
        }
        case 0x2: { // consecutive frame
            Message *m = openMessage(sender);
            if (!m)
                break;
            if ((pci & 0x0F) != (m->nextIndex & 0x0F))
                m->broken = true;
            ++m->nextIndex;
            m->bytes += frame.mid(1);
            if (m->bytes.size() >= m->length)
                m->open = false;
            break;
        }
        default: // flow control
            break;
        }
    }

    // ECU by ECU, in the order each first answered.
    QStringList result = passed;
    QStringList senders;
    for (const Message &m : std::as_const(found)) {
        if (!senders.contains(m.sender))
            senders << m.sender;
    }
    for (const QString &sender : std::as_const(senders)) {
        for (const Message &m : std::as_const(found)) {
            if (m.sender != sender || m.broken || m.bytes.size() < m.length)
                continue;
            result << QString::fromLatin1(m.bytes.left(m.length).toHex().toUpper());
        }
    }
    return result;
}

QHash<quint8, QByteArray> mode01Values(const QStringList &lines, const QHash<quint8, int> &lengths)
{
    QHash<quint8, QByteArray> values;
    for (const QByteArray &message : messages(lines)) {
        if (message.size() < 3 || quint8(message.at(0)) != 0x41)
            continue;
        QHash<quint8, QByteArray> found;
        qsizetype i = 1;
        bool exact = true;
        while (i < message.size()) {
            const auto pid = quint8(message.at(i));
            const int length = lengths.value(pid, -1);
            if (length <= 0 || i + 1 + length > message.size() || found.contains(pid)) {
                exact = false;
                break;
            }
            found.insert(pid, message.mid(i + 1, length));
            i += 1 + length;
        }
        if (!exact)
            continue;
        for (auto it = found.cbegin(); it != found.cend(); ++it) {
            if (!values.contains(it.key()))
                values.insert(it.key(), it.value());
        }
    }
    return values;
}

int mode01Responders(const QStringList &lines, quint8 pid)
{
    int count = 0;
    for (const QByteArray &message : messages(lines)) {
        if (message.size() > 2 && quint8(message.at(0)) == 0x41 && quint8(message.at(1)) == pid)
            ++count;
    }
    return count;
}

int mode01AnswerBytes(const QList<int> &dataLengths)
{
    int bytes = 1; // 41
    for (int length : dataLengths)
        bytes += 1 + length;
    return bytes;
}

bool takesResponseCount(const QStringList &lines)
{
    if (!errorIn(lines).isEmpty())
        return false;
    const QList<QByteArray> answers = messages(lines);
    return answers.size() == 1 && answers.first().size() == 6
           && answers.first().startsWith(QByteArray("\x41\x00", 2));
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

int canHeaderLength(const QString &dpn)
{
    QString code = compact(dpn);
    if (code.size() == 2 && code.startsWith(QLatin1Char('A')))
        code = code.mid(1);
    if (code == QLatin1String("6") || code == QLatin1String("8"))
        return 3;
    if (code == QLatin1String("7") || code == QLatin1String("9"))
        return 8;
    return 0;
}

} // namespace elm
