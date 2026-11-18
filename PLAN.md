# Plan: the next batch of features

The owner asked (18 Nov 2026) for all of the features below. Work paused
before any of them was started, to continue on another computer. Do them in
this order, one at a time, keeping the build, the tests and `all_qmllint`
clean after each, and tick them off here as they land.

Already done before the pause and committed: multi-PID polling on CAN
(`ObdSession::pollBatch`, `elm::stripCanHeaders`, `elm::mode01Values`, the
simulator's second ECU). It has not met a real car yet (see CLAUDE.md, Status).

## 1. Auto-connect on start, keep the screen on — done

Switches on the start page (Options) until the Settings page (10) exists.

- Settings `connect/auto` (default on) and `connect/last` (`bt`, `wifi`,
  `demo`, written by each `connect*`). `Q_INVOKABLE connectLast()` and a
  one-shot call when the app starts (ConnectPage `Component.onCompleted`,
  guarded so it runs once per process).
- Setting `view/keepScreenOn` (default on): while connected, set
  `FLAG_KEEP_SCREEN_ON` on the activity window. Java helper in
  `ObdBackground` (`keepScreenOn(Context, boolean)`, on the UI thread via
  `runOnUiThread`), a wrapper in `src/background.*`, called on state changes.
  This alone also stops the 15 s screen timeout that cut the first real
  recording into bursts.

## 2. Alerts while driving — done

Switch and engine speed limit under Options on the start page for now.

- Pure logic in new `src/alerts.*` (`alerts::Monitor`): fed every reading
  (pid, t, value; pid -1 = adapter voltage) and the check engine status;
  returns alerts {key, title, text}. Each alert fires once, clears with
  hysteresis, then may fire again after a cooldown (5 min; rpm 30 s).
  - Coolant (PID 05) >= 108 °C, clears below 103.
  - Not charging: engine running (rpm > 500) and voltage (PID 42, else ATRV)
    < 12.8 V for 20 s; clears above 13.2. Overcharging: > 15.3 V for 10 s.
  - Check engine light turned on or the code count went up: poll `0101`
    every 60 s while Running (byte A: bit 7 MIL, bits 0–6 count).
  - Engine speed >= a user limit (setting `alerts/rpm`, 0 = off).
  - Fuel level (PID 2F) < 10 %, once per connection.
- Delivery: a banner in the app (Main.qml, auto-hides) and on Android a
  high-importance notification on its own "Alerts" channel, so it sounds with
  the screen off (`ObdBackground.alert(...)`).
- Settings: `alerts/enabled` (default on), `alerts/rpm`.
- Tests: the monitor with synthetic readings.

## 3. Freeze frame (mode 02) — done

- `elm::mode02Payload(lines, pid, frame)` (answer `42 <pid> <frame> data`).
- After reading codes: `020000` (mask, continue with 2000, 4000 while bit 0
  is set), `020200` (the code that stored the frame), then `02<pid>00` for
  each supported PID in the table; decode with `obd::decode`.
- `Q_PROPERTY QVariantMap freezeFrame` (code + list of name/value), shown in
  CodesView under that code. Cleared with the codes (04).
- SimEcu: frame for P0133 (rpm 2240, 64 km/h, coolant 91, load 46 %, STFT +2,
  LTFT +9, MAP 52, IAT 24, throttle 18 %); NO DATA after clearing.

## 4. Drive history with cost — done

Drives shorter than 0.2 km and 2 minutes are not kept.

- FuelMeter: setting `fuel/price` (GEL per litre, 0 = off); every totals map
  gets `cost` ("4.85 ₾") and the Fuel tab shows it.
- Finished drives (start, end, km, litres, seconds; newest first, max 50) in
  `fuel/drives`. The current drive is persisted too (`fuel/drive/*` +
  start time) and finished when a new drive starts (`startSession(newDrive)`),
  on `disconnectAdapter()`, or at the next start if the app was killed.
- Fuel tab: a "Drives" card with Clear; price in the settings card.

## 5. Battery cranking test — done

The test ends 10 s after the start (charging is measured 5–10 s after it),
not after 6 s. The first 3 s measure the resting voltage, then the page asks
to start the engine.

- Pure `src/battery.*`: from samples {t, volts, rpm}: resting voltage before
  the start, minimum while cranking, charging voltage 5–10 s after
  rpm > 500. Verdicts: rest >= 12.4 ok, 12.2–12.4 watch, < 12.2 bad; cranking
  >= 10.0 ok, 9.6–10 watch, < 9.6 bad; charging 13.5–14.8 ok.
- ObdSession `startBatteryTest()`: needs Running with the engine off and the
  ignition on; while it runs `pollNext` only alternates `ATRV` and `010C` as
  fast as the adapter answers (no batching), until rpm > 500 for 6 s or 60 s.
- New Diagnostics tab "Battery" (BatteryView.qml): instructions, Start,
  results with verdicts.

## 6. Recording viewer

- Tap a recording in RecordingsPage → RecordingViewPage: summary tiles and
  one chart per column.
- C++ `RecordingViewer` (QML_ELEMENT, creatable, `path` property): parses the
  CSV (header "Name [unit]"), one SensorSeries per column. SensorSeries drops
  samples older than 15 min (`kRetentionSeconds` in sensorseries.cpp): add a
  retention setter. LineChart's right edge is `series->now()`, which without a
  clock is the last sample, so `timeSpan` = the recording's length shows all.
- Map columns back to PIDs by name (`obd::pidByName`, new) for the summary:
  duration, distance (integrate speed), fuel and L/100 km (fuel::chooseMethod
  and the integrator, current fuel settings), max speed and rpm, coolant max,
  voltage range, warm idle rpm, average fuel trims, gaps in the recording.

## 7. Custom sensors (mode 22 and others)

- Settings `custom/sensors`: list of {name, unit, header, command, formula,
  min, max}. Editor page (list, add/edit dialog, delete) reached from
  Settings; changes apply on the next connection.
- SensorModel entries with pid = -100 - index and new fields command, header,
  formula; paced Medium, never batched, ignored by fuel/health/alerts.
- Request: if a header is set (CAN only), `ATSH<header>`, the command, then
  restore `ATSH7DF` (11-bit) or `ATSH18DB33F1` (29-bit), queued back to back.
- Answer: `elm::customPayload(lines, command)`: the message that starts with
  (first command byte + 0x40) followed by the rest of the command bytes; the
  data after it.
- Formula with variables A–H (data bytes), e.g. `(A*256+B)/4`, compiled once
  with QJSEngine (`(function(A,B,C,D,E,F,G,H){return <formula>;})`).
- SimEcu: `ATSH` → OK; mode 22 DID 1105 from the transmission
  (`62 11 05 7B`, formula `A-40` → 83 °C) for the demo and the tests.
- No maker presets: DIDs differ per maker; the owner can enter them from
  community lists.

## 8. 0–100 km/h timer and HUD

- Pure `src/accel.*`: speed samples → states Idle → Ready (stopped) →
  Running → Done/Aborted; 0–60 and 0–100 times, interpolating the crossing
  between samples; start at the last zero-speed sample.
- `Q_PROPERTY QVariantList focusPids` set by pages: those PIDs are polled as
  if on screen (fast) and even with "Record all sensors" off.
- Timer page (⋮ menu): state text, big timer, last results and best.
- HUD page (⋮ menu): huge speed, rpm and consumption now, a mirror switch
  (for the windscreen reflection), full brightness while open
  (`ObdBackground.setBrightness`, window attribute screenBrightness).

## 9. Car Bluetooth reminder (instead of a fully automatic start)

Android does not let an app open itself from the background, and an ELM327
does not announce itself when it powers up, so a tap stays necessary:

- Manifest `BroadcastReceiver` for `BluetoothDevice.ACTION_ACL_CONNECTED`
  (allowed in the manifest despite Android 8's limits; needs BLUETOOTH_CONNECT,
  granted when the adapter was first connected). When the device is the one
  the owner picked (the car stereo; address in SharedPreferences, set from
  C++), post "Car connected – tap to start reading" opening the app.
- With auto-connect (1) the tap is all it takes.
- Settings: pick the trigger from the paired devices
  (`BluetoothAdapter.getBondedDevices()` through JNI, as JSON).

## 10. Settings page and Georgian

- SettingsPage (from ConnectPage and the dashboard menu): connection
  (connect on start, keep screen on, background), alerts, car Bluetooth
  reminder, custom sensors, language.
- Georgian translation of the whole UI (QML qsTr and C++ tr, health check
  texts included): `i18n/obdreadr_ka.ts`, `qt_add_translations` when
  `Qt6LinguistTools` is found (keep it optional so CI still builds without it;
  check the CI run). Locally it needs the `qt6-l10n-tools` package (lupdate,
  lrelease) – ask before installing it. Setting `ui/language` (system,
  English, ქართული): install the QTranslator in main.cpp and call
  `engine.retranslate()` when it changes.
- Do this last, once the strings of 1–9 exist.

## Finally

- README and CLAUDE.md for every feature; tests for the pure parts
  (alerts, battery, accel, freeze frame, custom payload and formula,
  recording parsing).
- Look at it with the demo car. A scratch copy of the repo with a patched
  `main.cpp` that drives `ObdSession` from timers and saves
  `window->grabWindow()` (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software)
  worked for screenshots. Leave the dashboard before `QCoreApplication::quit()`:
  Main.qml's `onClosing` (the Android back button) refuses to close while the
  dashboard is open, so quit is ignored there. The software renderer draws
  Material buttons and dialogs without their shadow layers; that is not a bug.
