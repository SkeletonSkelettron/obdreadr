#pragma once

#include <QString>

// The UI language: "" follows the phone, "en" English, "ka" Georgian
// (setting `ui/language`). The translations are compiled into the app
// (:/i18n/obdreadr_ka.qm) when Qt Linguist was found at build time.
namespace language {

QString setting();
void setSetting(const QString &code);

// Installs the translator for `code`; returns whether one was loaded.
bool apply(const QString &code);

} // namespace language
