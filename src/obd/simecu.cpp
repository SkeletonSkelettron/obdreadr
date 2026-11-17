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
    case 0x04:
        // Permanent codes stay until the car itself sees the fault gone.
        m_stored.clear();
        m_pending.clear();
        return prefix + hex(bytes({0x44}));
    default: break;
    }
    if (cmd.size() < 4)
        return prefix + QStringLiteral("NO DATA");
    const auto pid = quint8(cmd.mid(2, 2).toInt(nullptr, 16));

    if (mode == 0x01) {
        if (obd::isSupportMaskPid(pid)) {
            if (pid != 0x00 && !m_supported.contains(pid))
                return prefix + QStringLiteral("NO DATA");
            quint32 mask = 0;
            for (quint8 p : std::as_const(m_supported)) {
                if (p > pid && p <= pid + 0x20)
                    mask |= 0x80000000u >> (p - pid - 1);
            }
            QByteArray data = bytes({0x41, pid});
            data += char(mask >> 24);
            data += char(mask >> 16);
            data += char(mask >> 8);
            data += char(mask);
            return prefix + hex(data);
        }
        if (!m_supported.contains(pid))
            return prefix + QStringLiteral("NO DATA");
        const auto data = pidData(pid, t);
        if (!data)
            return prefix + QStringLiteral("NO DATA");
        return prefix + hex(bytes({0x41, pid}) + *data);
    }

    if (mode == 0x09 && pid == 0x02)
        return prefix + canAnswer(bytes({0x49, 0x02, 0x01}) + vin().toLatin1());

    return prefix + QStringLiteral("NO DATA");
}

QString SimEcu::canAnswer(const QByteArray &payload) const
{
    if (payload.size() <= 7)
        return hex(payload);
    // ISO-TP multi-frame answer, formatted the way an ELM327 prints it.
    QStringList lines;
    lines << QStringLiteral("%1").arg(payload.size(), 3, 16, QLatin1Char('0')).toUpper();
    int offset = 0;
    for (int frame = 0; offset < payload.size(); ++frame) {
        const int count = frame == 0 ? 6 : 7;
        const QByteArray chunk = payload.mid(offset, count);
        offset += count;
        lines << QStringLiteral("%1:").arg(frame & 0xF, 0, 16).toUpper()
                     + (m_spaces ? QStringLiteral(" ") : QString()) + hex(chunk);
    }
    return lines.join(QLatin1Char('\r'));
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
    const double maf = rpm * load / 4000.0;

    switch (pid) {
    case 0x01: return bytes({(m_stored.isEmpty() ? 0 : 0x80) | int(m_stored.size()), 0x07, 0xE5, 0x00});
    case 0x03: return bytes({0x02, 0x00});
    case 0x04: return bytes({u8(load * 255 / 100)});
    case 0x05: return bytes({u8(coolant + 40)});
    case 0x06: return bytes({u8((3 * std::sin(t * 1.3) + 100) * 128 / 100)});
    case 0x07: return bytes({u8((2.3 + 100) * 128 / 100)});
    case 0x0B: return bytes({u8(30 + load * 0.7)});
    case 0x0C: return u16(rpm * 4);
    case 0x0D: return bytes({u8(speed)});
    case 0x0E: return bytes({u8((8 + 12 * std::sin(t / 6) + 64) * 2)});
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
    QString out;
    if (m_headers)
        out = m_spaces ? QStringLiteral("7E8 %1 ").arg(data.size(), 2, 16, QLatin1Char('0'))
                       : QStringLiteral("7E8%1").arg(data.size(), 2, 16, QLatin1Char('0'));
    out += QString::fromLatin1(m_spaces ? data.toHex(' ') : data.toHex()).toUpper();
    return out;
}
