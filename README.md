# OBD Reader

An Android app, written in C++ with Qt 6, that reads live sensor data from a
car through an ELM327 OBD-II adapter and draws it as scrolling charts.

- **Finds the sensors by itself.** After connecting it asks the car which
  mode 01 PIDs it supports (PIDs 00, 20, 40, …) and lists every one it can
  decode: engine speed, vehicle speed, coolant, intake and oil temperatures,
  load, throttle, MAF, manifold pressure, fuel trims, O2 sensors, fuel level,
  fuel rate, control module voltage and about 80 more.
- **Faster on CAN cars** (most cars from 2008 on): up to six sensors are read
  with one request, which multiplies how often each sensor updates.
- **Bluetooth, BLE and Wi-Fi adapters.** Classic Bluetooth (SPP), Bluetooth
  Low Energy (finds the adapter's serial service by itself) and Wi-Fi
  (`192.168.0.10:35000` by default).
- **Four sensor views and a fuel tab**, each sensor in its own light color everywhere:
  - **Charts**: one card per sensor with its current value and a chart of the
    last 30 seconds to 10 minutes; tap a card to enlarge it.
  - **Values**: the same sensors as a list of names and current values.
  - **Overlay**: a few chosen sensors on one time axis. Each line has its own
    scale; tap a sensor in the legend to show its scale on the chart.
  - **Selected**: the chosen sensors' current values in large tiles.
  - **Fuel**: consumption in L/100 km right now (L/h when standing), and
    distance, fuel, average and time for a **measurement** you start and stop
    by hand, for this drive and in total over every drive, plus the earlier
    measurements. It uses the engine's fuel rate (PID 5E) when the car reports
    it, else estimates it from the air flow sensor (MAF) or, without one, from
    intake pressure, engine speed and the engine size you enter (petrol and
    LPG; a diesel needs the fuel rate PID). A correction in percent, found by
    comparing with a fill-up, scales every figure.

  Pick the charted sensors in ⋮ → Charted sensors and the compared ones in
  ⋮ → Compared sensors.
- **Diagnostics** (⋮ → Diagnostics), in four tabs:
  - **Codes**: the check engine light, stored (mode 03), pending (07) and
    permanent (0A) fault codes with short descriptions, and clearing them (04).
  - **Health**: checks on the live sensor data for problems that develop
    before a fault code: fuel trims (with a vacuum leak or fuel delivery
    pattern), charging and battery voltage, thermostat and overheating,
    temperature sensor agreement on a cold start, idle stability, air flow and
    load at idle, O2 sensor activity, catalyst efficiency from the sensors
    before and after it, knock-related timing retard and intake air heat.
    The limits are general rules of thumb.
  - **Readiness**: which of the car's emission self-tests have completed.
  - **Tests**: the car's own measurements (mode 06, CAN cars) against the
    limits at which it would set a code, and misfire counts per cylinder.
- **Recordings for a computer.** Every connection is saved as a CSV file
  (a time column, a timestamp column and one column per sensor, one row per
  reading). ⋮ → Recordings exports a file through Android's save dialog, to
  Downloads or a cloud drive. With **Record all sensors** (on by default) every
  sensor the car supports is read in the background, each at its own pace:
  fast-changing ones such as engine speed every second, slow ones such as
  temperatures every few seconds; sensors on screen are read more often.
- **Counts in the background.** On Android, while connected, a notification
  with the current figures keeps the app reading the car with the screen off
  or another app open (a foreground service with a wake lock). A connection
  that drops is retried every 15 seconds for 15 minutes after the car last
  answered, and the measurement and the total carry on. Leaving the dashboard
  disconnects. Some phones also need battery optimization off for the app
  (Fuel tab → Allow running in the background).
- **Connects by itself.** When the app starts it connects to the adapter
  used last, and the screen stays on while connected (both switchable under
  Options on the start page).
- **Demo car**, a built-in simulated ELM327, for trying the app without a car.
- Shows the VIN, the protocol and the battery voltage, and keeps a log of the
  conversation with the adapter (⋮ → Log).

### Writing to the car

Standard OBD-II gives an app one way to change the car: clearing the fault
codes. Coding and adaptations (changing settings in the car's modules) use
each maker's own diagnostic services, data identifiers and security access,
so they are not part of this app.

## Install

Every push to `master` builds an APK. Download `obdreadr-arm64-v8a.apk` from
the [Latest build](../../releases/tag/continuous) release on the phone and open
it (Android will ask to allow installing apps from the browser).

## Use

1. Plug the adapter into the OBD-II port (under the dashboard, near the
   steering column) and turn the ignition on.
2. Bluetooth: press **Scan** and pick the adapter. A classic Bluetooth adapter
   may ask to pair, with PIN `1234` or `0000`.
   Wi-Fi: join the adapter's Wi-Fi network, then **Connect**. If it won't
   connect, turn mobile data off; Android otherwise sends traffic over it.
3. The app initializes the adapter, finds the protocol and the supported
   sensors, and starts charting the common ones. Each sensor you add makes
   all of them update less often, as the adapter answers one request at a time.

## Build

Needs Qt 6.8 or newer with the Qt Bluetooth module (`qtconnectivity`).

Android (Qt for Android, JDK 17, Android SDK and NDK r27c):

```sh
<qt-android>/bin/qt-cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=<qt-desktop> -DANDROID_SDK_ROOT=<sdk> -DANDROID_NDK_ROOT=<ndk>
cmake --build build-android --target apk
```

Desktop (for development; Bluetooth is optional, Wi-Fi and the demo car always work):

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

The GitHub Actions workflow in `.github/workflows/build.yml` does both and
publishes the APK.

### Signing

CI signs the APK with `android/debug.keystore`, a debug key committed on
purpose so that each new build installs over the previous one. For a key of
your own, add the repository secrets `ANDROID_KEYSTORE_BASE64`,
`ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD`
(an app signed with a different key must be uninstalled once first).

## Code

| Path | What it does |
| --- | --- |
| `src/obd/pids.*` | Table of mode 01 PIDs: names, units, ranges and formulas |
| `src/obd/elmparser.*` | Parsing ELM327 answers: errors, supported-PID masks, VIN |
| `src/obd/dtc.*` | Fault codes: parsing modes 03/07/0A and short descriptions |
| `src/obd/monitors.*` | Readiness monitors and mode 06 test results |
| `src/health.*` | Health checks on the live sensor data |
| `src/fuel.*`, `src/fuelmeter.*` | Fuel consumption: estimates, integration, measurement, totals |
| `src/background.*`, `android/` | The Android foreground service (Java) and the manifest that declares it |
| `src/obd/simecu.*` | The simulated adapter and car used by the demo and the tests |
| `src/transport/*` | Byte pipes to the adapter: Bluetooth, BLE, TCP, simulator |
| `src/elm327.*` | Command queue: one command at a time, answer up to the `>` prompt |
| `src/obdsession.*` | Connecting, initializing, discovering sensors, polling |
| `src/recorder.*` | Writing each session's readings to a CSV file |
| `src/chartbase.*`, `src/linechart.*`, `src/overlaychart.*` | The charts, painted with `QQuickPaintedItem` |
| `qml/` | The user interface (Qt Quick Controls, Material style) |
| `tests/` | Unit tests of the parser (CAN headers, multi-PID answers), PID formulas, fault codes, monitors, health checks, fuel consumption, the recorder and the simulator |

## License

MIT, see [LICENSE](LICENSE).
