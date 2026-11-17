#include "obd/dtc.h"
#include "obd/elmparser.h"
#include "obd/pids.h"
#include "health.h"
#include "obd/monitors.h"
#include "obd/simecu.h"
#include "recorder.h"
#include "sensorseries.h"

#include <algorithm>

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

    void readsReadinessMonitors()
    {
        // Spark ignition, continuous monitors complete; catalyst, EVAP, O2 and
        // O2 heater supported, EVAP not complete.
        const QList<obd::Monitor> monitors = obd::readiness(QByteArray::fromHex("82076504"));
        const auto find = [&](const QString &name) {
            return *std::find_if(monitors.begin(), monitors.end(), [&](const obd::Monitor &m) { return m.name == name; });
        };
        QVERIFY(find(QStringLiteral("Misfire")).supported);
        QVERIFY(find(QStringLiteral("Misfire")).complete);
        QVERIFY(find(QStringLiteral("Catalyst")).supported);
        QVERIFY(find(QStringLiteral("Catalyst")).complete);
        QVERIFY(find(QStringLiteral("Evaporative system")).supported);
        QVERIFY(!find(QStringLiteral("Evaporative system")).complete);
        QVERIFY(!find(QStringLiteral("EGR / VVT")).supported);
        // Diesel: byte B bit 3 switches to the compression ignition names.
        const QList<obd::Monitor> diesel = obd::readiness(QByteArray::fromHex("000F4000"));
        QVERIFY(std::any_of(diesel.begin(), diesel.end(),
                            [](const obd::Monitor &m) { return m.name == QLatin1String("Particulate filter") && m.supported; }));
    }

    void parsesTestResults()
    {
        // Two records for monitor 01 in one multi-frame answer.
        const QStringList lines = {
            QStringLiteral("013"),
            QStringLiteral("0: 46 01 05 10 00 46"),
            QStringLiteral("1: 00 00 00 96 01 06 10"),
            QStringLiteral("2: 00 C8 00 00 00 96 00"),
        };
        const QList<obd::TestResult> results = obd::parseTests(lines);
        QCOMPARE(results.size(), 2);
        QCOMPARE(results[0].mid, quint8(0x01));
        QCOMPARE(results[0].tid, quint8(0x05));
        QCOMPARE(results[0].value, 70.0);
        QCOMPARE(results[0].max, 150.0);
        QCOMPARE(results[0].unit, QStringLiteral("ms"));
        QVERIFY(results[0].passed);
        QCOMPARE(results[1].value, 200.0);
        QVERIFY(!results[1].passed);
        QCOMPARE(obd::monitorName(0x01), QStringLiteral("O2 sensor, bank 1 sensor 1"));
        QCOMPARE(obd::monitorName(0x46), QStringLiteral("O2 sensor heater, bank 2 sensor 2"));
        QCOMPARE(obd::monitorName(0xA4), QStringLiteral("Misfire, cylinder 3"));
        QCOMPARE(obd::testName(0x01, 0x05), QStringLiteral("Rich to lean switch time"));
        // Signed temperature with a negative value.
        const QList<obd::TestResult> signedTemp = obd::parseTests({QStringLiteral("46 3D 81 96 FF 9C FE 0C 01 F4")});
        QCOMPARE(signedTemp.size(), 1);
        QCOMPARE(signedTemp[0].value, -10.0);
        QVERIFY(signedTemp[0].passed);
    }

    void readsSimulatedTestsAndReadiness()
    {
        SimEcu ecu;
        ask(ecu, QStringLiteral("ATE0"));
        ask(ecu, QStringLiteral("ATS0"));
        QList<quint8> mids;
        for (quint8 base = 0x00;; base += 0x20) {
            const auto mask = elm::supportedMask(ask(ecu, QStringLiteral("06") + obd::hex2(base)), base, 0x06);
            QVERIFY(mask);
            for (quint8 mid : obd::pidsFromMask(*mask, base)) {
                if (!obd::isSupportMaskPid(mid))
                    mids << mid;
            }
            if (!(*mask & 1u))
                break;
        }
        QCOMPARE(mids, (QList<quint8>{0x01, 0x21, 0xA2, 0xA3, 0xA4, 0xA5}));
        const QList<obd::TestResult> misfires = obd::parseTests(ask(ecu, QStringLiteral("06A4")));
        QCOMPARE(misfires.size(), 2);
        QCOMPARE(misfires[1].value, 31.0);
        QVERIFY(!misfires[1].hasLimits);

        const auto status = elm::mode01Payload(ask(ecu, QStringLiteral("0101")), 0x01);
        QVERIFY(status);
        const QList<obd::Monitor> before = obd::readiness(*status);
        ask(ecu, QStringLiteral("04"));
        const QList<obd::Monitor> after = obd::readiness(*elm::mode01Payload(ask(ecu, QStringLiteral("0101")), 0x01));
        const auto incomplete = [](const QList<obd::Monitor> &list) {
            return int(std::count_if(list.begin(), list.end(), [](const obd::Monitor &m) { return m.supported && !m.complete; }));
        };
        QCOMPARE(incomplete(before), 1);
        QCOMPARE(incomplete(after), 4);
        QVERIFY(elm::isCanProtocol(QStringLiteral("A6")));
        QVERIFY(!elm::isCanProtocol(QStringLiteral("3")));
    }

    void checksHealth()
    {
        // A warm engine idling unsteadily with lean trims and a weak alternator.
        SensorSeries rpm(QStringLiteral("rpm"), QString(), 0, 0, 8000, Qt::white, nullptr);
        SensorSeries speed(QStringLiteral("speed"), QString(), 0, 0, 255, Qt::white, nullptr);
        SensorSeries coolant(QStringLiteral("coolant"), QString(), 0, -40, 215, Qt::white, nullptr);
        SensorSeries shortTrim(QStringLiteral("stft"), QString(), 1, -100, 100, Qt::white, nullptr);
        SensorSeries longTrim(QStringLiteral("ltft"), QString(), 1, -100, 100, Qt::white, nullptr);
        SensorSeries volts(QStringLiteral("voltage"), QString(), 2, 0, 16, Qt::white, nullptr);
        for (int i = 0; i < 60; ++i) {
            const double t = i;
            rpm.append(t, i % 2 ? 700 : 860);
            speed.append(t, 0);
            coolant.append(t, 90);
            shortTrim.append(t, 4);
            longTrim.append(t, 14);
            volts.append(t, 12.8);
        }
        const health::Lookup lookup = [&](int pid) -> const SensorSeries * {
            switch (pid) {
            case 0x0C: return &rpm;
            case 0x0D: return &speed;
            case 0x05: return &coolant;
            case 0x06: return &shortTrim;
            case 0x07: return &longTrim;
            case 0x42: return &volts;
            default: return nullptr;
            }
        };
        const QList<health::Check> checks = health::evaluate(lookup, 59);
        const auto status = [&](const QString &title) {
            for (const health::Check &c : checks) {
                if (c.title == title)
                    return c.status;
            }
            return health::Status::NoData;
        };
        QCOMPARE(status(QStringLiteral("Battery and charging")), health::Status::Bad);  // 12.8 V running
        QCOMPARE(status(QStringLiteral("Fuel trim, bank 1")), health::Status::Bad);     // +18 %
        QCOMPARE(status(QStringLiteral("Idle")), health::Status::Bad);                  // 700/860 rpm
        QCOMPARE(status(QStringLiteral("Engine temperature")), health::Status::Ok);
        QCOMPARE(status(QStringLiteral("Air flow at idle")), health::Status::NoData);   // no MAF
        // Bank 2 is absent, so it has no check at all.
        QVERIFY(std::none_of(checks.begin(), checks.end(),
                             [](const health::Check &c) { return c.title == QLatin1String("Fuel trim, bank 2"); }));
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
