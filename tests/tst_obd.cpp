#include "obd/dtc.h"
#include "obd/elmparser.h"
#include "obd/pids.h"
#include "obd/simecu.h"
#include "recorder.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Runs a command against the simulator and returns the cleaned answer lines.
QStringList ask(SimEcu &ecu, const QString &command, double t = 10)
{
    const QByteArray raw = ecu.handle(command.toLatin1(), t);
    const qsizetype prompt = raw.indexOf('>');
    return elm::cleanLines(elm::splitLines(raw.left(prompt)), command);
}

} // namespace

class TestObd : public QObject
{
    Q_OBJECT

private slots:
    void splitsAndCleansLines()
    {
        const QByteArray raw("010C\r\rSEARCHING...\r41 0c 1a f8 \r\r");
        const QStringList lines = elm::cleanLines(elm::splitLines(raw), QStringLiteral("010C"));
        QCOMPARE(lines, QStringList{QStringLiteral("41 0C 1A F8")});
    }

    void findsErrors()
    {
        QCOMPARE(elm::errorIn({QStringLiteral("NO DATA")}), QStringLiteral("NO DATA"));
        QCOMPARE(elm::errorIn({QStringLiteral("UNABLE TO CONNECT")}), QStringLiteral("UNABLE TO CONNECT"));
        QCOMPARE(elm::errorIn({QStringLiteral("?")}), QStringLiteral("?"));
        QCOMPARE(elm::errorIn({QStringLiteral("BUS INIT: ...ERROR")}), QStringLiteral("BUS INIT ERROR"));
        QCOMPARE(elm::errorIn({QStringLiteral("410C1AF8")}), QString());
    }

    void readsSupportMaskFromSeveralEcus()
    {
        const QStringList lines = {QStringLiteral("4100BE3EA813"), QStringLiteral("41 00 80 00 00 01")};
        const auto mask = elm::supportedMask(lines, 0x00);
        QVERIFY(mask);
        QCOMPARE(*mask, 0xBE3EA813u | 0x80000001u);

        const QList<quint8> pids = obd::pidsFromMask(*mask, 0x00);
        QVERIFY(pids.contains(0x01));
        QVERIFY(pids.contains(0x0C));
        QVERIFY(pids.contains(0x0D));
        QVERIFY(pids.contains(0x20));
        QVERIFY(!pids.contains(0x02));
    }

    void ignoresAnswersForOtherPids()
    {
        QVERIFY(!elm::mode01Payload({QStringLiteral("410D32")}, 0x0C));
        QVERIFY(!elm::mode01Payload({QStringLiteral("NO DATA")}, 0x0C));
        const auto payload = elm::mode01Payload({QStringLiteral("410C1AF8")}, 0x0C);
        QVERIFY(payload);
        QCOMPARE(*payload, QByteArray::fromHex("1AF8"));
    }

    void decodesCommonPids()
    {
        QCOMPARE(*obd::decode(0x0C, QByteArray::fromHex("1AF8")), 1726.0);
        QCOMPARE(*obd::decode(0x0D, QByteArray::fromHex("32")), 50.0);
        QCOMPARE(*obd::decode(0x05, QByteArray::fromHex("7B")), 83.0);
        QCOMPARE(*obd::decode(0x11, QByteArray::fromHex("FF")), 100.0);
        QCOMPARE(*obd::decode(0x06, QByteArray::fromHex("80")), 0.0);
        QCOMPARE(*obd::decode(0x42, QByteArray::fromHex("3A98")), 15.0);
        QCOMPARE(*obd::decode(0x32, QByteArray::fromHex("FFFC")), -1.0);
        QCOMPARE(*obd::decode(0xA6, QByteArray::fromHex("0001E240")), 12345.6);
        QVERIFY(!obd::decode(0x0C, QByteArray::fromHex("1A"))); // too short
        QVERIFY(!obd::decode(0x01, QByteArray::fromHex("00000000"))); // bit field
    }

    void parsesCanVin()
    {
        const QStringList lines = {
            QStringLiteral("014"),
            QStringLiteral("0: 49 02 01 31 44 34"),
            QStringLiteral("1: 47 50 30 30 52 35 35"),
            QStringLiteral("2: 42 31 32 33 34 35 36"),
        };
        QCOMPARE(elm::parseVin(lines), QStringLiteral("1D4GP00R55B123456"));
    }

    void parsesLegacyVin()
    {
        const QStringList lines = {
            QStringLiteral("49 02 01 00 00 00 31"),
            QStringLiteral("49 02 02 44 34 47 50"),
            QStringLiteral("49 02 03 30 30 52 35"),
            QStringLiteral("49 02 04 35 42 31 32"),
            QStringLiteral("49 02 05 33 34 35 36"),
        };
        QCOMPARE(elm::parseVin(lines), QStringLiteral("1D4GP00R55B123456"));
    }

    void parsesVoltageAndProtocol()
    {
        QCOMPARE(*elm::parseVoltage({QStringLiteral("12.6V")}), 12.6);
        QVERIFY(!elm::parseVoltage({QStringLiteral("?")}));
        QCOMPARE(elm::protocolName(QStringLiteral("A6")), QStringLiteral("CAN 11 bit, 500 kbaud"));
        QCOMPARE(elm::protocolName(QStringLiteral("3")), QStringLiteral("ISO 9141-2"));
    }

    // The same steps ObdSession takes, against the simulated car.
    void discoversSimulatedCar()
    {
        SimEcu ecu;
        QVERIFY(ask(ecu, QStringLiteral("ATZ")).join(QLatin1Char(' ')).contains(QLatin1String("ELM327")));
        for (const char *command : {"ATE0", "ATL0", "ATS0", "ATH0", "ATSP0"})
            QCOMPARE(ask(ecu, QString::fromLatin1(command)), QStringList{QStringLiteral("OK")});

        QSet<quint8> supported;
        for (quint8 base = 0x00;; base += 0x20) {
            const auto mask = elm::supportedMask(ask(ecu, QStringLiteral("01") + obd::hex2(base)), base);
            QVERIFY2(mask, qPrintable(QStringLiteral("no mask for %1").arg(base)));
            for (quint8 pid : obd::pidsFromMask(*mask, base))
                supported.insert(pid);
            if (!(*mask & 1u))
                break;
        }
        for (quint8 pid : {0x04, 0x05, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x42, 0x5C, 0x5E})
            QVERIFY2(supported.contains(pid), qPrintable(obd::hex2(pid)));
        QVERIFY(!supported.contains(0x0A));
        QVERIFY(supported.contains(0x40));

        for (quint8 pid : std::as_const(supported)) {
            if (obd::isSupportMaskPid(pid) || !obd::findPid(pid))
                continue;
            const auto payload = elm::mode01Payload(ask(ecu, QStringLiteral("01") + obd::hex2(pid)), pid);
            QVERIFY2(payload, qPrintable(obd::hex2(pid)));
            QVERIFY2(obd::decode(pid, *payload), qPrintable(obd::hex2(pid)));
        }

        const auto rpm = obd::decode(0x0C, *elm::mode01Payload(ask(ecu, QStringLiteral("010C"), 42), 0x0C));
        QVERIFY(*rpm > 500 && *rpm < 8000);

        QCOMPARE(elm::errorIn(ask(ecu, QStringLiteral("010A"))), QStringLiteral("NO DATA"));
        QCOMPARE(elm::parseVin(ask(ecu, QStringLiteral("0902"))), SimEcu::vin());
        QCOMPARE(*elm::parseVoltage(ask(ecu, QStringLiteral("ATRV"))), 14.1);
    }

    void decodesTroubleCodes()
    {
        QCOMPARE(dtc::fromBytes(0x01, 0x33), QStringLiteral("P0133"));
        QCOMPARE(dtc::fromBytes(0x44, 0x20), QStringLiteral("C0420"));
        QCOMPARE(dtc::fromBytes(0x9A, 0xBC), QStringLiteral("B1ABC"));
        QCOMPARE(dtc::fromBytes(0xC1, 0x00), QStringLiteral("U0100"));
    }

    void parsesTroubleCodeAnswers()
    {
        // CAN, one frame: count byte, then pairs.
        QCOMPARE(dtc::parse({QStringLiteral("43 02 01 33 04 20")}, 0x03),
                 (QStringList{QStringLiteral("P0133"), QStringLiteral("P0420")}));
        QCOMPARE(dtc::parse({QStringLiteral("4300")}, 0x03), QStringList());
        // CAN, multi-frame, with padding after the last code.
        const QStringList multi = {
            QStringLiteral("00A"),
            QStringLiteral("0: 43 04 01 33 04 20"),
            QStringLiteral("1: 01 71 03 01 55 55 55"),
        };
        QCOMPARE(dtc::parse(multi, 0x03),
                 (QStringList{QStringLiteral("P0133"), QStringLiteral("P0420"), QStringLiteral("P0171"),
                              QStringLiteral("P0301")}));
        // Older protocols: three codes per line, zero padded; two ECUs.
        const QStringList legacy = {
            QStringLiteral("43 01 33 04 20 00 00"),
            QStringLiteral("43 01 33 00 00 00 00"),
        };
        QCOMPARE(dtc::parse(legacy, 0x03), (QStringList{QStringLiteral("P0133"), QStringLiteral("P0420")}));
        // Pending codes answer with 47, not 43.
        QCOMPARE(dtc::parse({QStringLiteral("47 01 01 71")}, 0x07), QStringList{QStringLiteral("P0171")});
        QCOMPARE(dtc::parse({QStringLiteral("47 01 01 71")}, 0x03), QStringList());
        QCOMPARE(dtc::parse({QStringLiteral("NO DATA")}, 0x0A), QStringList());
    }

    void describesTroubleCodes()
    {
        QCOMPARE(dtc::describe(QStringLiteral("P0420")), QStringLiteral("Catalyst efficiency below threshold, bank 1"));
        QCOMPARE(dtc::describe(QStringLiteral("P0304")), QStringLiteral("Cylinder 4 misfire detected"));
        QCOMPARE(dtc::describe(QStringLiteral("P1234")), QStringLiteral("Manufacturer-specific code"));
        QCOMPARE(dtc::describe(QStringLiteral("P0599")), QStringLiteral("Vehicle speed, idle control or auxiliary inputs"));
    }

    void readsAndClearsSimulatedCodes()
    {
        SimEcu ecu;
        ask(ecu, QStringLiteral("ATE0"));
        ask(ecu, QStringLiteral("ATS0"));
        const auto status = elm::mode01Payload(ask(ecu, QStringLiteral("0101")), 0x01);
        QVERIFY(status);
        QCOMPARE(quint8(status->at(0)), quint8(0x82)); // light on, two codes
        QCOMPARE(dtc::parse(ask(ecu, QStringLiteral("03")), 0x03),
                 (QStringList{QStringLiteral("P0133"), QStringLiteral("P0420")}));
        QCOMPARE(dtc::parse(ask(ecu, QStringLiteral("07")), 0x07), QStringList{QStringLiteral("P0171")});

        QCOMPARE(ask(ecu, QStringLiteral("04")), QStringList{QStringLiteral("44")});
        QCOMPARE(dtc::parse(ask(ecu, QStringLiteral("03")), 0x03), QStringList());
        QCOMPARE(dtc::parse(ask(ecu, QStringLiteral("0A")), 0x0A), QStringList{QStringLiteral("P0420")});
        QCOMPARE(quint8(elm::mode01Payload(ask(ecu, QStringLiteral("0101")), 0x01)->at(0)), quint8(0x00));
    }

    void pacesSensors()
    {
        QCOMPARE(obd::pace(0x0C), obd::Pace::Fast);
        QCOMPARE(obd::pace(0x15), obd::Pace::Fast);
        QCOMPARE(obd::pace(0x05), obd::Pace::Slow);
        QCOMPARE(obd::pace(0x2F), obd::Pace::Slow);
        QCOMPARE(obd::pace(0x42), obd::Pace::Medium);
    }

    void recordsCsv()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Recorder recorder;
        const QDateTime start(QDate(2026, 11, 17), QTime(14, 0, 0));
        const QStringList columns = {QStringLiteral("Engine speed [rpm]"), QStringLiteral("Trim, bank \"1\" [%]")};
        QVERIFY(recorder.start(dir.path(), QStringLiteral("Demo car"), columns, start));
        QVERIFY(recorder.isRecording());
        recorder.record(0, 1.5, 1726.25);
        recorder.record(1, 2.0, -3.125);
        recorder.record(5, 2.5, 1.0); // no such column: ignored
        recorder.stop();

        const QFileInfo info(recorder.filePath());
        QVERIFY(info.fileName().startsWith(QLatin1String("obd_")));
        QVERIFY(info.fileName().endsWith(QLatin1String("_Demo-car.csv")));
        QFile file(info.absoluteFilePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = file.readAll().split('\n');
        QCOMPARE(lines.size(), 4); // header, two rows, final newline
        QCOMPARE(lines[0], QByteArray("time_s,timestamp,\"Engine speed [rpm]\",\"Trim, bank \"\"1\"\" [%]\""));
        QCOMPARE(lines[1], QByteArray("1.500,2026-11-17 14:00:01.500,1726.25,"));
        QCOMPARE(lines[2], QByteArray("2.000,2026-11-17 14:00:02.000,,-3.125"));
    }

    void keepsNewestRecordings()
    {
        QTemporaryDir dir;
        Recorder recorder;
        for (int i = 0; i < 4; ++i) {
            QVERIFY(recorder.start(dir.path(), QStringLiteral("car"), {QStringLiteral("x")}, QDateTime::currentDateTime(), 2));
            recorder.stop();
        }
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("obd_*.csv")}, QDir::Files).size(), 2);
    }

    void simulatorEchoesUntilTurnedOff()
    {
        SimEcu ecu;
        const QByteArray withEcho = ecu.handle("ATI", 0);
        QVERIFY(withEcho.startsWith("ATI\r"));
        QVERIFY(withEcho.endsWith('>'));
        ecu.handle("ATE0", 0);
        QVERIFY(!ecu.handle("ATI", 0).startsWith("ATI"));
    }
};

QTEST_GUILESS_MAIN(TestObd)
#include "tst_obd.moc"
