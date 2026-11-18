#include "obd/simecu.h"

#include "obd/pids.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

quint8 u8(double v)
{
    return quint8(std::clamp(std::lround(v), 0L, 255L));
}

QByteArray u16(double v)
{
    const auto raw = quint16(std::clamp(std::lround(v), 0L, 65535L));
    QByteArray out;
    out += char(raw >> 8);
    out += char(raw & 0xFF);
    return out;
}

QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for (int v : values)
        out += char(v);
    return out;
}

const char kBanner[] = "ELM327 v1.5";
constexpr quint16 kEngine = 0x7E8;
constexpr quint16 kTransmission = 0x7E9;
constexpr quint8 kTransmissionPid = 0x0D; // the one sensor both ECUs report

} // namespace

SimEcu::SimEcu()
{
    m_supported = {0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                   0x10, 0x11, 0x13, 0x14, 0x1C, 0x1F, 0x20, 0x21, 0x2F, 0x33, 0x40,
                   0x42, 0x46, 0x49, 0x5C, 0x5E};
}

QString SimEcu::vin()
{
    return QStringLiteral("OBDREADRSIM000001");
}

QByteArray SimEcu::handle(const QByteArray &command, double t)
{
    const QString cmd = QString::fromLatin1(command).simplified().remove(QLatin1Char(' ')).toUpper();
    QString reply;
    if (cmd.isEmpty())
        reply = QString();
    else if (cmd.startsWith(QLatin1String("AT")))
        reply = atCommand(cmd.mid(2));
    else
        reply = obdCommand(cmd, t);

    QByteArray out;
    if (m_echo)
        out += command + '\r';
    out += reply.toLatin1();
    out += "\r\r>";
    return out;
}

QString SimEcu::atCommand(const QString &cmd)
{
    if (cmd == QLatin1String("Z") || cmd == QLatin1String("WS")) {
        m_echo = true;
        m_spaces = true;
        m_headers = false;
        m_searched = false;
        return QStringLiteral("\r") + QLatin1String(kBanner);
    }
    if (cmd == QLatin1String("I"))
        return QLatin1String(kBanner);
    if (cmd == QLatin1String("E0") || cmd == QLatin1String("E1")) {
        m_echo = cmd.endsWith(QLatin1Char('1'));
        return QStringLiteral("OK");
    }
    if (cmd == QLatin1String("S0") || cmd == QLatin1String("S1")) {
        m_spaces = cmd.endsWith(QLatin1Char('1'));
        return QStringLiteral("OK");
    }
    if (cmd == QLatin1String("H0") || cmd == QLatin1String("H1")) {
        m_headers = cmd.endsWith(QLatin1Char('1'));
        return QStringLiteral("OK");
    }
    if (cmd == QLatin1String("RV"))
        return QStringLiteral("14.1V");
    if (cmd == QLatin1String("DPN"))
        return QStringLiteral("A6");
    if (cmd == QLatin1String("DP"))
        return QStringLiteral("AUTO, ISO 15765-4 (CAN 11/500)");
    if (cmd == QLatin1String("D") || cmd.startsWith(QLatin1String("SP")) || cmd.startsWith(QLatin1String("TP"))
        || cmd.startsWith(QLatin1String("L")) || cmd.startsWith(QLatin1String("AT"))
        || cmd.startsWith(QLatin1String("ST")) || cmd.startsWith(QLatin1String("CAF")))
        return QStringLiteral("OK");
    return QStringLiteral("?");
}

QString SimEcu::obdCommand(const QString &cmd, double t)
{
    bool ok = false;
    cmd.toULongLong(&ok, 16);
    if (!ok || cmd.size() < 2 || cmd.size() % 2 != 0)
        return QStringLiteral("?");

    QString prefix;
    if (!m_searched) {
        m_searched = true;
        prefix = QStringLiteral("SEARCHING...\r");
    }

    const int mode = cmd.left(2).toInt(nullptr, 16);
    switch (mode) {
    case 0x03: return prefix + codesAnswer(0x03, m_stored);
    case 0x07: return prefix + codesAnswer(0x07, m_pending);
    case 0x0A: return prefix + codesAnswer(0x0A, m_permanent);
    case 0x06:
        if (cmd.size() >= 4)
            return prefix + testsAnswer(quint8(cmd.mid(2, 2).toInt(nullptr, 16)));
        break;
    case 0x04:
        // Permanent codes stay until the car itself sees the fault gone.
        m_stored.clear();
        m_incomplete = 0x65; // clearing resets every monitor
        m_pending.clear();
        return prefix + canAnswer(bytes({0x44}));
    default: break;
    }
    if (cmd.size() < 4)
        return prefix + QStringLiteral("NO DATA");
    const auto pid = quint8(cmd.mid(2, 2).toInt(nullptr, 16));

    if (mode == 0x01) {
        QList<quint8> pids;
        for (qsizetype i = 2; i + 2 <= cmd.size() && pids.size() < 6; i += 2)
            pids << quint8(cmd.mid(i, 2).toInt(nullptr, 16));
        return prefix + mode01(pids, t);
    }

    if (mode == 0x02)
        return prefix + freezeFrameAnswer(pid, cmd.size() >= 6 ? quint8(cmd.mid(4, 2).toInt(nullptr, 16)) : 0);

    if (mode == 0x09 && pid == 0x02)
        return prefix + canAnswer(bytes({0x49, 0x02, 0x01}) + vin().toLatin1());

    return prefix + QStringLiteral("NO DATA");
}

QString SimEcu::mode01(const QList<quint8> &pids, double t) const
{
    const auto maskAnswer = [this](quint8 base, const QSet<quint8> &supported) {
        quint32 mask = 0;
        for (quint8 p : supported) {
            if (p > base && p <= base + 0x20)
                mask |= 0x80000000u >> (p - base - 1);
        }
        QByteArray data = bytes({0x41, base});
        data += char(mask >> 24);
        data += char(mask >> 16);
        data += char(mask >> 8);
        data += char(mask);
        return data;
    };
    if (pids.size() == 1 && obd::isSupportMaskPid(pids.first())) {
        const quint8 base = pids.first();
        if (base != 0x00 && !m_supported.contains(base))
            return QStringLiteral("NO DATA");
        QList<Message> answers = {{kEngine, maskAnswer(base, m_supported)}};
        if (base == 0x00)
            answers << Message{kTransmission, maskAnswer(base, {kTransmissionPid})};
        return reply(answers);
    }
    // Each ECU answers the PIDs it has, in one message.
    QByteArray engine = bytes({0x41});
    QByteArray transmission = bytes({0x41});
    for (quint8 pid : pids) {
        if (obd::isSupportMaskPid(pid))
            continue;
        const auto data = m_supported.contains(pid) ? pidData(pid, t) : std::nullopt;
        if (!data)
            continue;
        engine += char(pid) + *data;
        if (pid == kTransmissionPid)
            transmission += char(pid) + *data;
    }
    QList<Message> answers;
    if (engine.size() > 1)
        answers << Message{kEngine, engine};
    if (transmission.size() > 1)
        answers << Message{kTransmission, transmission};
    return answers.isEmpty() ? QStringLiteral("NO DATA") : reply(answers);
}

QString SimEcu::canAnswer(const QByteArray &payload) const
{
    return reply({{kEngine, payload}});
}

QString SimEcu::reply(const QList<Message> &messages) const
{
    const QString gap = m_spaces ? QStringLiteral(" ") : QString();
    QStringList lines;
    if (!m_headers) {
        for (const Message &m : messages) {
            if (m.payload.size() <= 7) {
                lines << hex(m.payload);
                continue;
            }
            // ISO-TP multi-frame answer, formatted the way an ELM327 prints it.
            lines << QStringLiteral("%1").arg(m.payload.size(), 3, 16, QLatin1Char('0')).toUpper();
            int offset = 0;
            for (int frame = 0; offset < m.payload.size(); ++frame) {
                const int count = frame == 0 ? 6 : 7;
                const QByteArray chunk = m.payload.mid(offset, count);
                offset += count;
                lines << QStringLiteral("%1:").arg(frame & 0xF, 0, 16).toUpper() + gap + hex(chunk);
            }
        }
        return lines.join(QLatin1Char('\r'));
    }

    // With headers: CAN frames with the PCI byte; the last one padded.
    QList<QList<QByteArray>> frames;
    for (const Message &m : messages) {
        QList<QByteArray> list;
        const QByteArray &p = m.payload;
        if (p.size() <= 7) {
            list << char(p.size()) + p;
        } else {
            list << bytes({0x10 | (int(p.size()) >> 8), int(p.size()) & 0xFF}) + p.left(6);
            int index = 1;
            for (qsizetype offset = 6; offset < p.size(); offset += 7, ++index) {
                QByteArray frame = char(0x20 | (index & 0x0F)) + p.mid(offset, 7);
                frame.append(8 - frame.size(), '\0');
                list << frame;
            }
        }
        frames << list;
    }
    // The ECUs answer at the same time, so their frames interleave.
    for (qsizetype round = 0;; ++round) {
        bool any = false;
        for (qsizetype i = 0; i < frames.size(); ++i) {
            if (round >= frames[i].size())
                continue;
            any = true;
            lines << QString::number(messages[i].sender, 16).toUpper() + gap + hex(frames[i][round]);
        }
        if (!any)
            break;
    }
    return lines.join(QLatin1Char('\r'));
}

QString SimEcu::testsAnswer(quint8 mid) const
{
    // Monitors with test results; 00, 20, ... are the support masks.
    static const QList<quint8> kMids = {0x01, 0x20, 0x21, 0x40, 0x60, 0x80, 0xA0, 0xA2, 0xA3, 0xA4, 0xA5};
    if (obd::isSupportMaskPid(mid)) {
        if (mid != 0x00 && !kMids.contains(mid))
            return QStringLiteral("NO DATA");
        quint32 mask = 0;
        for (quint8 m : kMids) {
            if (m > mid && m <= mid + 0x20)
                mask |= 0x80000000u >> (m - mid - 1);
        }
        QByteArray data = bytes({0x46, mid});
        data += char(mask >> 24);
        data += char(mask >> 16);
        data += char(mask >> 8);
        data += char(mask);
        return canAnswer(data);
    }
    struct Record {
        int tid, uasid, value, min, max;
    };
    QList<Record> records;
    switch (mid) {
    case 0x01: // O2 sensor switch times in ms
        records = {{0x05, 0x10, 70, 0, 150}, {0x06, 0x10, 85, 0, 150}};
        break;
    case 0x21: // catalyst: maker-defined index, two thirds of the way to its limit
        records = {{0x80, 0x01, 0x0150, 0x0000, 0x0200}};
        break;
    case 0xA4: // cylinder 3 misfires now and then, below the fault code limit
        records = {{0x0B, 0x24, 12, 0, 0}, {0x0C, 0x24, 31, 0, 0}};
        break;
    case 0xA2:
    case 0xA3:
    case 0xA5:
        records = {{0x0B, 0x24, 0, 0, 0}, {0x0C, 0x24, 0, 0, 0}};
        break;
    default:
        return QStringLiteral("NO DATA");
    }
    QByteArray payload = bytes({0x46});
    for (const Record &r : records) {
        payload += bytes({mid, r.tid, r.uasid, r.value >> 8, r.value & 0xFF, r.min >> 8, r.min & 0xFF,
                          r.max >> 8, r.max & 0xFF});
    }
    return canAnswer(payload);
}

QString SimEcu::freezeFrameAnswer(quint8 pid, quint8 frame) const
{
    if (frame != 0 || !m_stored.contains(0x0133))
        return QStringLiteral("NO DATA");
    static const QList<quint8> kPids = {0x02, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0F, 0x11};
    QByteArray data;
    switch (pid) {
    case 0x00: {
        quint32 mask = 0;
        for (quint8 p : kPids)
            mask |= 0x80000000u >> (p - 1);
        data = bytes({int(mask >> 24), int((mask >> 16) & 0xFF), int((mask >> 8) & 0xFF), int(mask & 0xFF)});
        break;
    }
    case 0x02: data = bytes({0x01, 0x33}); break;                     // P0133
    case 0x04: data = bytes({u8(46 * 255 / 100.0)}); break;            // load 46 %
    case 0x05: data = bytes({91 + 40}); break;                         // coolant 91 °C
    case 0x06: data = bytes({u8((2 + 100) * 128 / 100.0)}); break;     // short trim +2 %
    case 0x07: data = bytes({u8((9 + 100) * 128 / 100.0)}); break;     // long trim +9 %
    case 0x0B: data = bytes({52}); break;                              // 52 kPa
    case 0x0C: data = u16(2240 * 4); break;                            // 2240 rpm
    case 0x0D: data = bytes({64}); break;                              // 64 km/h
    case 0x0F: data = bytes({24 + 40}); break;                         // intake air 24 °C
    case 0x11: data = bytes({u8(18 * 255 / 100.0)}); break;            // throttle 18 %
    default: return QStringLiteral("NO DATA");
    }
    return canAnswer(bytes({0x42, pid, frame}) + data);
}

QString SimEcu::codesAnswer(quint8 service, const QList<quint16> &codes) const
{
    QByteArray payload = bytes({service + 0x40, int(codes.size())});
    for (quint16 code : codes) {
        payload += char(code >> 8);
        payload += char(code & 0xFF);
    }
    return canAnswer(payload);
}

std::optional<QByteArray> SimEcu::pidData(quint8 pid, double t) const
{
    const double speed = std::max(0.0, 55 + 45 * std::sin(t / 23.0) + 8 * std::sin(t / 3.7));
    const double rpm = speed < 3 ? 780 + 20 * std::sin(t * 2) : 900 + speed * 28 + 250 * std::sin(t / 1.9);
    const double throttle = std::clamp(12 + 30 * (0.5 + 0.5 * std::sin(t / 5.0)), 0.0, 100.0);
    const double load = std::clamp(throttle * 1.4 + 10, 0.0, 100.0);
    const double coolant = std::min(90.0, 25 + t * 0.6) + std::sin(t / 7);
    const double maf = rpm * load / 10000.0; // a 1.6 litre engine: 3–4 g/s at idle

    switch (pid) {
    // Spark ignition; misfire, fuel and components monitors complete;
    // catalyst, EVAP, O2 sensor and heater supported.
    case 0x01: return bytes({(m_stored.isEmpty() ? 0 : 0x80) | int(m_stored.size()), 0x07, 0x65, m_incomplete});
    case 0x03: return bytes({0x02, 0x00});
    case 0x04: return bytes({u8(load * 255 / 100)});
    case 0x05: return bytes({u8(coolant + 40)});
    // A small vacuum leak: leaner at idle than when driving.
    case 0x06: return bytes({u8((3 * std::sin(t * 1.3) + (speed < 3 ? 6 : 0) + 100) * 128 / 100)});
    case 0x07: return bytes({u8((9.4 + 100) * 128 / 100)});
    case 0x0B: return bytes({u8(30 + load * 0.7)});
    case 0x0C: return u16(rpm * 4);
    case 0x0D: return bytes({u8(speed)});
    case 0x0E: return bytes({u8((30 - load * 0.25 + 2 * std::sin(t / 6) + 64) * 2)});
    case 0x0F: return bytes({u8(28 + 4 * std::sin(t / 40) + 40)});
    case 0x10: return u16(maf * 100);
    case 0x11: return bytes({u8(throttle * 255 / 100)});
    case 0x13: return bytes({0x03});
    case 0x14: return bytes({u8((0.45 + 0.4 * std::sin(t * 3.1)) * 200), u8(128)});
    case 0x1C: return bytes({0x06});
    case 0x1F: return u16(t);
    case 0x21: return u16(0);
    case 0x2F: return bytes({u8((63 - t * 0.01) * 255 / 100)});
    case 0x33: return bytes({101});
    case 0x42: return u16((14.1 + 0.1 * std::sin(t)) * 1000);
    case 0x46: return bytes({u8(18 + 40)});
    case 0x49: return bytes({u8(throttle * 0.9 * 255 / 100)});
    case 0x5C: return bytes({u8(std::min(95.0, 22 + t * 0.45) + 40)});
    case 0x5E: return u16(maf * 0.331 * 20);
    default: return std::nullopt;
    }
}

QString SimEcu::hex(const QByteArray &data) const
{
    return QString::fromLatin1(m_spaces ? data.toHex(' ') : data.toHex()).toUpper();
}
