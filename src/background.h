#pragma once

#include <QString>

// Keeps the app running while the phone's screen is off or another app is
// open: on Android, a foreground service with an ongoing notification and a
// partial wake lock (android/src/.../ObdService.java). Elsewhere it does nothing.
namespace background {

bool supported();

// Starts the service, or updates its notification if it runs already.
void start(const QString &title, const QString &text);
void update(const QString &title, const QString &text);
void stop();

// Keeps the phone's screen from turning off (the activity window's
// FLAG_KEEP_SCREEN_ON) while `on`.
void keepScreenOn(bool on);

// Android's battery optimization may still stop an app in the background on
// some phones; this asks the user to exempt it.
bool batteryRestricted();
void requestUnrestricted();

} // namespace background
