# OBD Reader

An Android app, written in C++ with Qt 6, that reads live sensor data from a
car through an ELM327 OBD-II adapter and draws it as scrolling charts.

- **Finds the sensors by itself.** After connecting it asks the car which
  mode 01 PIDs it supports (PIDs 00, 20, 40, …) and lists every one it can
  decode: engine speed, vehicle speed, coolant, intake and oil temperatures,
  load, throttle, MAF, manifold pressure, fuel trims, O2 sensors, fuel level,
  fuel rate, control module voltage and about 80 more.
- **Bluetooth, BLE and Wi-Fi adapters.** Classic Bluetooth (SPP), Bluetooth
  Low Energy (finds the adapter's serial service by itself) and Wi-Fi
  (`192.168.0.10:35000` by default).
- **Charts** of the last 30 seconds to 10 minutes, one card per sensor. Tap a
  card to enlarge it; pick sensors in the ⋮ → Sensors panel.
- **Demo car**, a built-in simulated ELM327, for trying the app without a car.
- Shows the VIN, the protocol and the battery voltage, and keeps a log of the
  conversation with the adapter (⋮ → Log).

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
| `src/obd/simecu.*` | The simulated adapter and car used by the demo and the tests |
| `src/transport/*` | Byte pipes to the adapter: Bluetooth, BLE, TCP, simulator |
| `src/elm327.*` | Command queue: one command at a time, answer up to the `>` prompt |
| `src/obdsession.*` | Connecting, initializing, discovering sensors, polling |
| `src/linechart.*` | The chart, a `QQuickPaintedItem` |
| `qml/` | The user interface (Qt Quick Controls, Material style) |
| `tests/` | Unit tests of the parser, the PID formulas and the simulator |

## License

MIT, see [LICENSE](LICENSE).
