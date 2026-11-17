# OBD Reader — notes for Claude

An Android app in C++17 / Qt 6 (Qt Quick, Material style) that talks to a car
through an ELM327 OBD-II adapter: it finds the sensors the car supports, polls
them, charts them, records them to CSV and runs diagnostics. README.md is the
user-facing description; this file is how the code works and how to keep
working on it.

The owner talks to Claude in Georgian; answer in Georgian and keep it short.
The app's UI and the code (identifiers, comments, commit messages) are English.

## Status

Released: v0.2.0 (colors, four views, fault codes, CSV recording/export),
v0.3.0 (Diagnostics: health checks, readiness, mode 06) and v0.4.0 (Fuel
tab, background service with automatic reconnect). Every push to
`master` also refreshes the rolling "continuous" pre-release, which the owner
installs on the phone from
`https://github.com/SkeletonSkelettron/obdreadr/releases/download/continuous/obdreadr-arm64-v8a.apk`.

The first version was confirmed working on a phone with a real car. The
features after it (views, codes, recording, diagnostics, fuel, background
service) pass the unit tests and the demo car but have not been reported back
from a real car yet. Things most likely to need a fix once tried:

- **CSV export** uses `FileDialog` in SaveFile mode, which on Android should
  open the system "create document" dialog and return a `content://` URL that
  `QFile` writes through the content resolver (`ObdSession::exportRecording`).
  If it misbehaves, the alternative is an Android share intent (needs a
  FileProvider in a custom manifest).
- **BLE adapters** (`BleTransport`) have never met real hardware; classic
  Bluetooth SPP is the one that worked.
- **Mode 06 scaling** of rarer unit/scaling IDs (`src/obd/monitors.cpp`);
  pass/fail is computed on raw values, so only the displayed numbers could be off.
- **Health check limits** (`src/health.cpp`) are general rules of thumb.
- **Background service** (`android/`, `src/background.cpp`): never run on a
  phone. Check that polling goes on with the screen off (Qt's event loop keeps
  running because of `android.app.background_running` in the manifest), that
  the notification appears (Android 13+ asks for the permission on the first
  connection) and that an automatic reconnect works. Phones with aggressive
  battery savers may still need "unrestricted" battery for the app.
- **Several PIDs per request** (`ObdSession::pollBatch`, `elm::stripCanHeaders`):
  never met a real car. On CAN the session turns headers on (`ATH1`) after
  `ATDPN`, and every answer is converted back to the header-less format, so
  if a real adapter prints headers differently, single polls could break too.
  The first real-car recording (17 Nov 2026, before this) showed ~6 requests/s
  for 34 sensors, so each sensor every ~3 s; the demo car went from ~24 to ~95
  readings/s.
- **Fuel estimates** (`src/fuel.cpp`): the MAF and speed-density ones ignore
  deceleration fuel cut-off, so they read a little high; the correction
  setting is there for that. Speed-density assumes 85 % volumetric efficiency.

## Layout and data flow

```
Transport (BT classic / BLE / TCP / simulator)
   └─ Elm327: one command at a time, answer collected up to the '>' prompt
        └─ ObdSession (QML singleton): connect → init → discover → poll
             ├─ SensorModel: one Entry per sensor, each with a SensorSeries
             │     (15 min of samples) and flags active / pinned / available
             ├─ SensorFilterModel ×2: active sensors (Charts, Values tabs),
             │     pinned sensors (Overlay, Selected tabs)
             ├─ Recorder: every reading to a CSV file per session
             ├─ health::evaluate every 2 s on the series
             ├─ FuelMeter: distance and fuel from the readings (now, drive,
             │     measurement, total), its sensors always polled
             ├─ background::start/update/stop → ObdService (Android)
             └─ fault codes, readiness, mode 06 on request
QML (qml/): Main → ConnectPage → DashboardPage (5 tabs) → DiagnosticsPage,
            RecordingsPage, LogPage; ConnectPage → FuelPage, RecordingsPage
```

| Path | What it does |
| --- | --- |
| `src/obd/pids.*` | Mode 01 PID table (name, unit, range, decimals, formula, favorite) and `pace()` |
| `src/obd/elmparser.*` | ELM327 text: lines, errors, payloads, support masks, ISO-TP messages, CAN headers, multi-PID answers, VIN |
| `src/obd/dtc.*` | Fault codes (modes 03/07/0A) and their descriptions |
| `src/obd/monitors.*` | Readiness (PID 01) and mode 06 test results |
| `src/obd/simecu.*` | Simulated adapter + car (engine 7E8, transmission 7E9), headers on or off, multi-PID: demo mode and the tests |
| `src/transport/*` | Byte pipes; Bluetooth ones only built when Qt Bluetooth exists |
| `src/elm327.*` | Command queue with timeouts; after a timeout it drains the late answer; strips CAN headers once set |
| `src/obdsession.*` | The state machine and everything QML calls |
| `src/sensormodel.*`, `src/sensorseries.*` | Sensors, their samples and colors |
| `src/recorder.*` | CSV writer, keeps the newest 50 files in AppDataLocation/recordings |
| `src/health.*` | Health checks on live data |
| `src/fuel.*` | Fuel flow from fuel rate, MAF or speed-density; trapezoid integrator |
| `src/fuelmeter.*` | Fuel tab's data: now, this drive, measurement, total, history; persisted |
| `src/background.*` | JNI calls into `ObdBackground.java`; no-ops off Android |
| `android/` | Custom manifest (Qt 6.10 template + service, permissions, background running), Java service, notification icon, debug keystore |
| `src/chartbase.*`, `linechart.*`, `overlaychart.*` | `QQuickPaintedItem` charts |
| `tests/tst_obd.cpp` | QtTest unit tests of all the pure logic |

Things worth knowing before changing them:

- **Stale callbacks.** Every connection bumps `ObdSession::m_generation`; every
  `Elm327::send` callback captures it and returns early when it no longer
  matches. Keep that pattern for any new request.
- **Polling** (`ObdSession::pollNext`) picks the sensor most overdue relative to
  `pollIntervalMs()`: fast/medium/slow pace from `obd::pace()`, shorter when
  the sensor is on screen. With "Record all sensors" (setting `record/all`,
  default on) sensors that are not on screen are polled too. Other requests
  (codes, mode 06) just queue between polls.
- **Several PIDs per request** (CAN protocols 6–9 only): after `ATDPN` the
  session sends `ATH1` and `Elm327::setCanHeaderLength(3 or 8)`, so every
  answer goes through `elm::stripCanHeaders` (frames regrouped per ECU, one
  hex line per message) and the rest of the parsing is unchanged. A sensor
  joins combined requests once its single answer gave its data length; the
  most overdue sensor goes out with up to five others at least half due
  (`01 0C 0D 10 ...`), split by `elm::mode01Values`, which drops any message
  that does not split exactly. A sensor missing twice from combined answers
  is read on its own; three combined requests with nothing usable turn
  combining off for the connection. Misses there never make a sensor
  unavailable.
- A sensor answering NO DATA six times in a row becomes unavailable; twelve
  failures in a row across all sensors ends the session.
- **Charts** repaint only while visible (`ChartBase::requestRepaint`); the
  dashboard uses a `StackLayout`, not a `SwipeView`, so hidden tabs stay idle.
- Each sensor's color comes from `SensorSeries::paletteColor(index)` (light
  Material 200 tones for the dark theme) and is used in every view.
- **Adding a PID:** a row in `kPids` (`pids.cpp`), its pace in `pace()`, and,
  so the demo and tests cover it, an answer in `SimEcu::pidData`.
- **Adding any protocol feature:** teach `SimEcu` to answer it and add a test
  that runs it against the simulator.
- **Fuel meter.** `ObdSession` feeds every reading to `FuelMeter::addSample`;
  the meter picks a method from the available PIDs and the fuel type, and its
  `inputs()` are polled even with "Record all sensors" off (fast while the Fuel
  tab is on screen). Distance and fuel are integrated per sample with a
  10 s maximum gap. Fuel is stored raw; the correction is applied when shown.
  "This drive" starts from zero only on a connection the user starts, not on
  Retry or an automatic reconnect.
- **Background.** With `background/enabled` (default on, Android only) a
  connection starts `ObdService` (foreground, type connectedDevice, partial
  wake lock); the notification text is refreshed every 5 s. `fail()` schedules
  a reconnect every 15 s while the car answered within the last 15 minutes;
  `disconnectAdapter()` stops both. Android does not let an app start a
  foreground service from the background, so an automatic reconnect only
  updates the running one. When upgrading Qt, compare `android/AndroidManifest.xml`
  with the new `qtbase/src/android/templates/AndroidManifest.xml`.
- Settings (QSettings): `wifi/*`, `bt/*`, `sensors/active`, `sensors/pinned`,
  `view/window`, `view/tab`, `record/all`, `background/enabled`, `fuel/type`,
  `fuel/engineLiters`, `fuel/correction`, `fuel/total/*`, `fuel/trip/*`,
  `fuel/history`.

## Build and test

Desktop (development; Bluetooth is optional, Wi-Fi and the demo car always
work):

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target all_qmllint   # QML lint, keep it warning-free
```

Android: Qt for Android 6.10 (with qtconnectivity), JDK 17, Android SDK and
NDK r27c:

```sh
<qt-android>/bin/qt-cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=<qt-desktop> -DANDROID_SDK_ROOT=<sdk> -DANDROID_NDK_ROOT=<ndk>
cmake --build build-android --target apk
```

On the owner's work PC Qt for Android is not installed and AppLocker blocks
running the built executables, so there only the desktop compile and the QML
lint were run locally; the tests and the APK came from CI. The home PC
(Ubuntu) has the distribution's Qt 6.10 dev packages, cmake and ninja, so the
desktop build, the tests and the QML lint run there; no Android toolchain.

QML conventions: `pragma ComponentBehavior: Bound`, delegates with `required`
properties, ids qualified — that keeps `all_qmllint` clean.

## CI and releases

`.github/workflows/build.yml`:

- **desktop**: Ubuntu, Qt 6.10.3 with qtconnectivity, builds and runs the tests.
- **android**: builds the APK for arm64-v8a, signs it with
  `android/debug.keystore` (a debug key committed on purpose so every build
  installs over the last one; repository secrets `ANDROID_KEYSTORE_*` would
  replace it), uploads it as an artifact.
- **release**: on a push to `master` it replaces the `continuous`
  pre-release; on a `v*` tag it creates a versioned release.

To release a version: bump `project(... VERSION x.y.z)` in CMakeLists.txt,
push to master, then `git tag -a vX.Y.Z -m "OBD Reader X.Y.Z"` and push the tag.

There is no `gh` CLI on the work PC. CI can be followed through the public API,
for example:

```sh
curl -s "https://api.github.com/repos/SkeletonSkelettron/obdreadr/actions/runs?head_sha=$(git rev-parse HEAD)&per_page=1"
curl -s "https://api.github.com/repos/SkeletonSkelettron/obdreadr/actions/runs/<run>/jobs"
curl -s "https://api.github.com/repos/SkeletonSkelettron/obdreadr/check-runs/<job>/annotations"
```

Job logs need a signed-in admin, but annotations are public: when configuring
or building the APK fails, the workflow puts the matching output lines into an
error annotation. Unauthenticated API calls are limited to 60 an hour.

## Git

- Remote `git@github.com:SkeletonSkelettron/obdreadr.git` over SSH, branch `master`.
- Commit with the same author identity as the existing commits (`git log -1`),
  which is the owner's personal one, not a work identity.
- Commit messages: an imperative title, then prose paragraphs on what changed
  and why, ending with the `Co-Authored-By:` line for Claude.
- Push only when the owner asks; they usually ask for a release right after.

## Ideas not done yet

- Keep the screen on while connected (Android window flag via JNI).
- An app icon (`QT_ANDROID_PACKAGE_SOURCE_DIR` with res/ drawables).
- Freeze frame data (mode 02) next to each stored code.
- Manufacturer-specific data (mode 22) for a given make; writing settings
  (coding) was deliberately left out because it is maker-specific and risky.
- Detecting deceleration fuel cut-off (fuel system status, PID 03) to
  improve the MAF and speed-density estimates.
- Starting by itself when the adapter appears (a boot or Bluetooth receiver
  and a Qt service process); today the app must be opened to connect.
- A Georgian translation of the UI (strings are already in `tr`/`qsTr`).
