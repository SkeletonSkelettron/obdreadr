#include "alerts.h"
#include "battery.h"
#include "obd/dtc.h"
#include "obd/elmparser.h"
#include "obd/pids.h"
#include "fuel.h"
#include "fuelmeter.h"
#include "health.h"
#include "obd/monitors.h"
#include "obd/simecu.h"
#include "recorder.h"
#include "sensorseries.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
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
    void initTestCase()
    {
        // The fuel meter keeps its figures in QSettings; not the user's.
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("obdreadr-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_obd"));
    }

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

    void estimatesFuelFlow()
    {
        // 10 g/s of air at 14.7:1 is 0.68 g/s of petrol, 3.31 L/h at 740 g/L.
        QCOMPARE(qRound(fuel::litersPerHour(10, fuel::Kind::Petrol) * 100), 331);
        // A rich mixture burns more; a nonsense lambda is ignored.
        QVERIFY(fuel::litersPerHour(10, fuel::Kind::Petrol, 0.85) > 3.8);
        QCOMPARE(fuel::litersPerHour(10, fuel::Kind::Petrol, 0), fuel::litersPerHour(10, fuel::Kind::Petrol));
        // 2.0 L at 3000 rpm, 100 kPa and 25 °C: 0.05 m³/s, 2.02 mol/s, 49.7 g/s at 85 %.
        QCOMPARE(qRound(fuel::speedDensityAir(100, 3000, 25, 2.0) * 10), 497);
        QCOMPARE(fuel::speedDensityAir(100, 0, 25, 2.0), 0.0);
    }

    void choosesFuelMethod()
    {
        using fuel::Kind;
        using fuel::Method;
        QCOMPARE(fuel::chooseMethod({0x0D, 0x10, 0x5E}, Kind::Petrol), Method::FuelRate);
        QCOMPARE(fuel::chooseMethod({0x0D, 0x10, 0x0B, 0x0C}, Kind::Petrol), Method::Maf);
        QCOMPARE(fuel::chooseMethod({0x0D, 0x0B, 0x0C}, Kind::Lpg), Method::SpeedDensity);
        QCOMPARE(fuel::chooseMethod({0x0D, 0x10}, Kind::Diesel), Method::None);
        QCOMPARE(fuel::chooseMethod({0x0D, 0x5E}, Kind::Diesel), Method::FuelRate);
        QCOMPARE(fuel::chooseMethod({0x10, 0x5E}, Kind::Petrol), Method::None); // no speed
        QCOMPARE(fuel::inputs(Method::Maf, {0x0D, 0x10, 0x0C}), (QList<int>{0x10, 0x0D}));
        QCOMPARE(fuel::inputs(Method::SpeedDensity, {0x0B, 0x0C, 0x0D, 0x0F}), (QList<int>{0x0B, 0x0C, 0x0F, 0x0D}));
        QCOMPARE(fuel::inputs(Method::None, {0x0D, 0x10}), QList<int>{0x0D});
    }

    void integratesWithGaps()
    {
        fuel::Integrator integrator(10);
        QCOMPARE(integrator.add(0, 60).area, 0.0);
        QCOMPARE(integrator.add(1, 80).area, 70.0);   // trapezoid
        QCOMPARE(integrator.add(2, 80).seconds, 1.0);
        QCOMPARE(integrator.add(30, 80).area, 0.0);   // a gap adds nothing
        QCOMPARE(integrator.add(31, 40).area, 60.0);
        integrator.reset();
        QCOMPARE(integrator.add(32, 40).area, 0.0);
    }

    void measuresFuelOverATrip()
    {
        {
            FuelMeter meter(nullptr);
            meter.resetTotal();
            meter.clearHistory();
            meter.setCorrection(100);
            meter.startSession({0x0D, 0x5E, 0x10}, true);
            QCOMPARE(meter.method(), fuel::Method::FuelRate);
            QCOMPARE(meter.inputs(), (QList<int>{0x5E, 0x0D}));

            // 30 s before the measurement, then 60 s at 60 km/h burning 6 L/h.
            for (int t = 0; t <= 90; ++t) {
                meter.addSample(0x0D, t, 60);
                meter.addSample(0x5E, t, 6);
                meter.addSample(0x10, t, 99); // not the method: ignored
                if (t == 30)
                    meter.startTrip();
            }
            meter.stopTrip();
            QCOMPARE(qRound(meter.tripTotals().km * 1000), 1000);       // 1 km
            QCOMPARE(qRound(meter.tripTotals().liters * 1000), 100);    // 0.1 L
            QCOMPARE(qRound(meter.tripTotals().per100km() * 10), 100);  // 10 L/100 km
            QCOMPARE(qRound(meter.driveTotals().km * 1000), 1500);
            QVERIFY(!meter.tripRunning());
            QCOMPARE(meter.history().size(), 1);
            QCOMPARE(meter.trip().value(QStringLiteral("average")).toString(), QStringLiteral("10.0"));

            // Samples after the measurement count for the drive only; a long
            // gap counts for nothing.
            meter.addSample(0x0D, 120, 60);
            meter.addSample(0x0D, 121, 60);
            QCOMPARE(qRound(meter.driveTotals().km * 10000), 15167);
            QCOMPARE(qRound(meter.tripTotals().km * 1000), 1000);

            meter.setCorrection(110); // shown figures only
            QCOMPARE(meter.trip().value(QStringLiteral("average")).toString(), QStringLiteral("11.0"));
            meter.setCorrection(100);
            meter.endSession();
            QVERIFY(!meter.connected());
            meter.addSample(0x0D, 130, 60); // not connected: ignored
        }
        // The total and the measurements are still there after a restart.
        FuelMeter restarted(nullptr);
        QCOMPARE(qRound(restarted.totalTotals().km * 10000), 15167);
        QCOMPARE(qRound(restarted.tripTotals().liters * 1000), 100);
        QCOMPARE(restarted.history().size(), 1);
        restarted.startSession({0x0D, 0x5E}, false);
        QCOMPARE(qRound(restarted.driveTotals().km), 0); // a new process starts a new drive
    }

    void keepsDrivesWithCost()
    {
        {
            FuelMeter meter(nullptr);
            meter.clearDrives();
            meter.setCorrection(100);
            meter.setPrice(3.0);
            meter.startSession({0x0D, 0x5E}, true);
            // 10 minutes at 60 km/h burning 6 L/h: 10 km, 1 L.
            for (int t = 0; t <= 600; ++t) {
                meter.addSample(0x0D, t, 60);
                meter.addSample(0x5E, t, 6);
            }
            QCOMPARE(meter.drive().value(QStringLiteral("cost")).toString(), QStringLiteral("3.00 ₾"));
            meter.endSession();
            meter.finishDrive();
            QCOMPARE(meter.drives().size(), 1);
            QCOMPARE(meter.drives().first().toMap().value(QStringLiteral("distance")).toString(),
                     QStringLiteral("10.0 km"));
            QCOMPARE(qRound(meter.driveTotals().km), 0);

            // A connection too short to count is not kept.
            meter.startSession({0x0D, 0x5E}, true);
            meter.addSample(0x0D, 700, 30);
            meter.addSample(0x0D, 701, 30);
            meter.finishDrive();
            QCOMPARE(meter.drives().size(), 1);

            // A drive the app was stopped in is finished at the next start.
            meter.startSession({0x0D, 0x5E}, true);
            for (int t = 800; t <= 1100; ++t)
                meter.addSample(0x0D, t, 36);
            meter.save();
        }
        FuelMeter restarted(nullptr);
        QCOMPARE(restarted.drives().size(), 2);
        QCOMPARE(restarted.drives().first().toMap().value(QStringLiteral("distance")).toString(),
                 QStringLiteral("3.00 km"));
        restarted.setPrice(0);
        QCOMPARE(restarted.drives().first().toMap().value(QStringLiteral("cost")).toString(), QString());
    }

    void estimatesFuelOnSimulatedCar()
    {
        // The demo car reports both its fuel rate and its air flow; the
        // estimate from air flow should agree with the fuel rate.
        SimEcu ecu;
        ask(ecu, QStringLiteral("ATE0"));
        ask(ecu, QStringLiteral("ATS0"));
        FuelMeter byRate(nullptr);
        FuelMeter byAir(nullptr);
        byRate.startSession({0x0D, 0x5E}, true);
        byAir.startSession({0x0D, 0x10}, true);
        QCOMPARE(byAir.method(), fuel::Method::Maf);
        for (double t = 0; t <= 600; t += 0.5) {
            for (quint8 pid : {quint8(0x0D), quint8(0x10), quint8(0x5E)}) {
                const auto payload = elm::mode01Payload(ask(ecu, QStringLiteral("01") + obd::hex2(pid), t), pid);
                QVERIFY(payload);
                const double value = *obd::decode(pid, *payload);
                byRate.addSample(pid, t, value);
                byAir.addSample(pid, t, value);
            }
        }
        const fuel::Totals rate = byRate.driveTotals();
        const fuel::Totals air = byAir.driveTotals();
        QVERIFY(rate.km > 5);
        QCOMPARE(qRound(rate.km * 1000), qRound(air.km * 1000));
        QVERIFY2(std::abs(rate.liters - air.liters) / rate.liters < 0.01,
                 qPrintable(QStringLiteral("%1 L vs %2 L").arg(rate.liters).arg(air.liters)));
        const double average = rate.per100km();
        QVERIFY2(average > 3 && average < 20, qPrintable(QString::number(average)));
    }

    void stripsCanHeaders()
    {
        // Two ECUs answer PID 00 in one frame each.
        const QStringList masks = elm::stripCanHeaders(
            {QStringLiteral("7E8 06 41 00 BE 3F A8 13"), QStringLiteral("7E9 06 41 00 00 08 00 00")}, 3);
        QCOMPARE(masks, (QStringList{QStringLiteral("4100BE3FA813"), QStringLiteral("410000080000")}));
        QCOMPARE(*elm::supportedMask(masks, 0x00), 0xBE3FA813u | 0x00080000u);

        // The engine's two frames with the transmission's frame between them;
        // the padding of the last frame is dropped.
        const QStringList interleaved = {
            QStringLiteral("7E8 10 0B 41 0C 1A F8 0D 32"),
            QStringLiteral("7E9 03 41 0D 32"),
            QStringLiteral("7E8 21 10 01 2C 11 40 00 00"),
        };
        QCOMPARE(elm::stripCanHeaders(interleaved, 3),
                 (QStringList{QStringLiteral("410C1AF80D3210012C1140"), QStringLiteral("410D32")}));
        // Without spaces, and 29-bit identifiers.
        QCOMPARE(elm::stripCanHeaders({QStringLiteral("7E804410C1AF8")}, 3), QStringList{QStringLiteral("410C1AF8")});
        QCOMPARE(elm::stripCanHeaders({QStringLiteral("18 DA F1 10 04 41 0C 1A F8")}, 8),
                 QStringList{QStringLiteral("410C1AF8")});
        // Errors and header-less lines pass; a frame out of sequence spoils its message.
        QCOMPARE(elm::stripCanHeaders({QStringLiteral("NO DATA")}, 3), QStringList{QStringLiteral("NO DATA")});
        QCOMPARE(elm::stripCanHeaders({QStringLiteral("410C1AF8")}, 3), QStringList{QStringLiteral("410C1AF8")});
        QCOMPARE(elm::stripCanHeaders({QStringLiteral("7E8 10 0B 41 0C 1A F8 0D 32"),
                                       QStringLiteral("7E8 22 10 01 2C 11 40 00 00")}, 3),
                 QStringList());
        QCOMPARE(elm::canHeaderLength(QStringLiteral("A6")), 3);
        QCOMPARE(elm::canHeaderLength(QStringLiteral("9")), 8);
        QCOMPARE(elm::canHeaderLength(QStringLiteral("A3")), 0);
    }

    void splitsMultiPidAnswers()
    {
        const QHash<quint8, int> lengths = {{0x0C, 2}, {0x0D, 1}, {0x10, 2}, {0x11, 1}};
        const QStringList lines = {QStringLiteral("410C1AF80D3210012C1140"), QStringLiteral("410D33")};
        const QHash<quint8, QByteArray> values = elm::mode01Values(lines, lengths);
        QCOMPARE(values.size(), 4);
        QCOMPARE(values.value(0x0C), QByteArray::fromHex("1AF8"));
        QCOMPARE(values.value(0x0D), QByteArray::fromHex("32")); // the first ECU's
        QCOMPARE(values.value(0x10), QByteArray::fromHex("012C"));
        QCOMPARE(values.value(0x11), QByteArray::fromHex("40"));
        // The header-less multi-frame format works the same way.
        const QStringList framed = {QStringLiteral("00B"), QStringLiteral("0: 41 0C 1A F8 0D 32"),
                                    QStringLiteral("1: 10 01 2C 11 40 00 00")};
        QCOMPARE(elm::mode01Values(framed, lengths).size(), 4);
        // A message that does not split exactly is not trusted at all.
        QVERIFY(elm::mode01Values({QStringLiteral("410C1AF80D")}, lengths).isEmpty());
        QVERIFY(elm::mode01Values({QStringLiteral("410C1AF805")}, lengths).isEmpty()); // 05: length unknown
        QVERIFY(elm::mode01Values({QStringLiteral("NO DATA")}, lengths).isEmpty());
    }

    void readsSeveralPidsFromSimulatedCar()
    {
        SimEcu ecu;
        for (const char *command : {"ATE0", "ATS0", "ATH1"})
            ask(ecu, QString::fromLatin1(command));
        const auto askCan = [&](const QString &command, double t) {
            return elm::stripCanHeaders(ask(ecu, command, t), 3);
        };
        // Both ECUs answer; the transmission reports the speed too.
        const QStringList raw = ask(ecu, QStringLiteral("010D"), 30);
        QCOMPARE(raw.size(), 2);
        QVERIFY(raw[1].startsWith(QLatin1String("7E9")));

        // Lengths from single requests, then one request for six PIDs.
        const QList<quint8> pids = {0x0C, 0x0D, 0x10, 0x11, 0x05, 0x5E};
        QHash<quint8, int> lengths;
        QHash<quint8, double> single;
        for (quint8 pid : pids) {
            const auto payload = elm::mode01Payload(askCan(QStringLiteral("01") + obd::hex2(pid), 30), pid);
            QVERIFY2(payload, qPrintable(obd::hex2(pid)));
            lengths.insert(pid, int(payload->size()));
            single.insert(pid, *obd::decode(pid, *payload));
        }
        QString command = QStringLiteral("01");
        for (quint8 pid : pids)
            command += obd::hex2(pid);
        const QStringList frames = ask(ecu, command, 30);
        QVERIFY(frames.size() >= 3); // first frame, the transmission's, consecutive frames
        QVERIFY(frames[1].startsWith(QLatin1String("7E9")));
        const QHash<quint8, QByteArray> values = elm::mode01Values(elm::stripCanHeaders(frames, 3), lengths);
        QCOMPARE(values.size(), pids.size());
        for (quint8 pid : pids)
            QCOMPARE(*obd::decode(pid, values.value(pid)), single.value(pid));

        // A PID the car lacks is left out; one it has none of is NO DATA.
        QCOMPARE(elm::mode01Values(askCan(QStringLiteral("010C0A"), 30), lengths).size(), 1);
        QCOMPARE(elm::errorIn(askCan(QStringLiteral("010A"), 30)), QStringLiteral("NO DATA"));
        // Everything else still reads through the header stripping.
        QCOMPARE(elm::parseVin(askCan(QStringLiteral("0902"), 30)), SimEcu::vin());
        QCOMPARE(dtc::parse(askCan(QStringLiteral("03"), 30), 0x03),
                 (QStringList{QStringLiteral("P0133"), QStringLiteral("P0420")}));
        QCOMPARE(obd::parseTests(askCan(QStringLiteral("06A4"), 30)).size(), 2);
        QVERIFY(elm::supportedMask(askCan(QStringLiteral("0100"), 30), 0x00));
    }

    void readsSimulatedFreezeFrame()
    {
        SimEcu ecu;
        ask(ecu, QStringLiteral("ATE0"));
        ask(ecu, QStringLiteral("ATS0"));
        const auto mask = elm::mode02Payload(ask(ecu, QStringLiteral("020000")), 0x00, 0);
        QVERIFY(mask);
        QCOMPARE(mask->size(), 4);
        const quint32 bits = quint32(quint8(mask->at(0))) << 24 | quint32(quint8(mask->at(1))) << 16
                             | quint32(quint8(mask->at(2))) << 8 | quint32(quint8(mask->at(3)));
        const QList<quint8> pids = obd::pidsFromMask(bits, 0x00);
        QVERIFY(pids.contains(0x02));
        QVERIFY(pids.contains(0x0C));
        QVERIFY(!(bits & 1u)); // no PIDs past 20

        const auto code = elm::mode02Payload(ask(ecu, QStringLiteral("020200")), 0x02, 0);
        QVERIFY(code);
        QCOMPARE(dtc::fromBytes(quint8(code->at(0)), quint8(code->at(1))), QStringLiteral("P0133"));
        const auto rpm = elm::mode02Payload(ask(ecu, QStringLiteral("020C00")), 0x0C, 0);
        QVERIFY(rpm);
        QCOMPARE(*obd::decode(0x0C, *rpm), 2240.0);
        QCOMPARE(*obd::decode(0x0D, *elm::mode02Payload(ask(ecu, QStringLiteral("020D00")), 0x0D, 0)), 64.0);
        QCOMPARE(*obd::decode(0x05, *elm::mode02Payload(ask(ecu, QStringLiteral("020500")), 0x05, 0)), 91.0);
        // A live answer does not pass for a frame, and the frame goes with the codes.
        QVERIFY(!elm::mode02Payload(ask(ecu, QStringLiteral("010C")), 0x0C, 0));
        ask(ecu, QStringLiteral("04"));
        QCOMPARE(elm::errorIn(ask(ecu, QStringLiteral("020200"))), QStringLiteral("NO DATA"));
    }

    void testsTheBattery()
    {
        // 12.6 V at rest, a dip to 10.2 V while cranking, 14.2 V running.
        battery::Test test(100);
        double t = 100;
        for (; t < 103; t += 0.2) {
            test.addVolts(t, 12.6);
            test.addRpm(t + 0.1, 0);
        }
        test.tick(103);
        QCOMPARE(test.phase(), battery::Phase::Crank);
        for (; t < 106; t += 0.2) {
            test.addVolts(t, t < 104 ? 12.5 : 10.2 + (t - 104));
            test.addRpm(t + 0.1, t < 105.5 ? 200 : 900);
        }
        QCOMPARE(test.phase(), battery::Phase::Running);
        QVERIFY(test.engineStarted());
        for (; t < 120; t += 0.2) {
            test.addVolts(t, 14.2);
            test.addRpm(t + 0.1, 800);
        }
        QCOMPARE(test.phase(), battery::Phase::Done);
        QCOMPARE(test.rest(), 12.6);
        QCOMPARE(qRound(test.cranking() * 10), 102);
        QCOMPARE(test.charging(), 14.2);
        QCOMPARE(battery::Test::restVerdict(test.rest()), battery::Verdict::Ok);
        QCOMPARE(battery::Test::crankVerdict(test.cranking()), battery::Verdict::Ok);
        QCOMPARE(battery::Test::chargeVerdict(test.charging()), battery::Verdict::Ok);
        QCOMPARE(battery::Test::restVerdict(12.3), battery::Verdict::Watch);
        QCOMPARE(battery::Test::crankVerdict(9.4), battery::Verdict::Bad);
        QCOMPARE(battery::Test::chargeVerdict(13.2), battery::Verdict::Watch);
        QCOMPARE(battery::Test::chargeVerdict(-1), battery::Verdict::None);

        // Already running: stops at once. Never started: gives up after a minute.
        battery::Test running(0);
        running.addRpm(0.5, 800);
        QCOMPARE(running.phase(), battery::Phase::Done);
        QVERIFY(running.engineWasRunning());
        battery::Test idle(0);
        idle.addVolts(1, 12.4);
        idle.tick(70);
        QCOMPARE(idle.phase(), battery::Phase::Done);
        QVERIFY(!idle.engineStarted());
        QCOMPARE(idle.rest(), 12.4);
        QCOMPARE(idle.cranking(), -1.0);
    }

    void alertsOnOverheating()
    {
        alerts::Monitor monitor;
        QVERIFY(monitor.addReading(0x05, 0, 100).isEmpty());
        const QList<alerts::Alert> hot = monitor.addReading(0x05, 1, 108);
        QCOMPARE(hot.size(), 1);
        QCOMPARE(hot[0].key, QStringLiteral("coolant"));
        QVERIFY(monitor.addReading(0x05, 2, 110).isEmpty()); // still the same episode
        QVERIFY(monitor.addReading(0x05, 3, 104).isEmpty()); // not yet cleared
        QVERIFY(monitor.addReading(0x05, 4, 102).isEmpty()); // cleared
        QVERIFY(monitor.addReading(0x05, 5, 109).isEmpty()); // within the cooldown
        QCOMPARE(monitor.addReading(0x05, 400, 109).size(), 1);
    }

    void alertsOnCharging()
    {
        alerts::Monitor monitor;
        QList<alerts::Alert> raised;
        for (int t = 0; t <= 25; ++t) {
            raised << monitor.addReading(0x0C, t, 800);
            raised << monitor.addReading(0x42, t, 12.5);
        }
        QCOMPARE(raised.size(), 1); // after 20 s, once
        QCOMPARE(raised[0].key, QStringLiteral("charging"));
        // The adapter's reading is ignored once the car reports its voltage.
        QVERIFY(monitor.addReading(-1, 26, 16.0).isEmpty());

        // With the engine off a low voltage is no alert.
        alerts::Monitor parked;
        for (int t = 0; t <= 30; ++t)
            QVERIFY(parked.addReading(-1, t, 12.1).isEmpty());

        // Overcharging after 10 s.
        alerts::Monitor high;
        raised.clear();
        for (int t = 0; t <= 12; ++t)
            raised << high.addReading(-1, t, 15.6);
        QCOMPARE(raised.size(), 1);
        QCOMPARE(raised[0].key, QStringLiteral("overcharging"));
    }

    void alertsOnCheckEngineRpmAndFuel()
    {
        alerts::Monitor monitor;
        QVERIFY(monitor.setCheckEngine(0, false, 0).isEmpty()); // baseline
        QCOMPARE(monitor.setCheckEngine(60, true, 1).size(), 1);
        QVERIFY(monitor.setCheckEngine(120, true, 1).isEmpty());
        QCOMPARE(monitor.setCheckEngine(180, true, 2).size(), 1);

        // A light already on at connection is the baseline, not an alert.
        alerts::Monitor lit;
        QVERIFY(lit.setCheckEngine(0, true, 1).isEmpty());

        monitor.setRpmLimit(4000);
        QVERIFY(monitor.addReading(0x0C, 0, 3900).isEmpty());
        QCOMPARE(monitor.addReading(0x0C, 1, 4100).size(), 1);
        QVERIFY(monitor.addReading(0x0C, 2, 4200).isEmpty());
        QVERIFY(monitor.addReading(0x0C, 3, 3700).isEmpty()); // cleared
        QVERIFY(monitor.addReading(0x0C, 10, 4100).isEmpty()); // cooldown
        QCOMPARE(monitor.addReading(0x0C, 40, 4100).size(), 1);

        QVERIFY(monitor.addReading(0x2F, 0, 12).isEmpty());
        QCOMPARE(monitor.addReading(0x2F, 1, 9).size(), 1);
        QVERIFY(monitor.addReading(0x2F, 2, 8).isEmpty()); // once per connection
        monitor.reset();
        QCOMPARE(monitor.rpmLimit(), 4000.0); // the limit is a setting, kept
        QCOMPARE(monitor.addReading(0x2F, 3, 9).size(), 1);
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

private:
    QTemporaryDir m_settingsDir;
};

QTEST_GUILESS_MAIN(TestObd)
#include "tst_obd.moc"
