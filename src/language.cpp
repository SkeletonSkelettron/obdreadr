#include "language.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace language {

namespace {
QTranslator *g_translator = nullptr;
} // namespace

QString setting()
{
    return QSettings().value(QStringLiteral("ui/language")).toString();
}

void setSetting(const QString &code)
{
    QSettings().setValue(QStringLiteral("ui/language"), code);
}

bool apply(const QString &code)
{
    if (g_translator) {
        QCoreApplication::removeTranslator(g_translator);
        delete g_translator;
        g_translator = nullptr;
    }
    const bool georgian = code == QLatin1String("ka")
                          || (code.isEmpty() && QLocale::system().language() == QLocale::Georgian);
    if (!georgian)
        return false;
    auto *translator = new QTranslator;
    if (!translator->load(QStringLiteral(":/i18n/obdreadr_ka.qm"))) {
        delete translator;
        return false;
    }
    QCoreApplication::installTranslator(translator);
    g_translator = translator;
    return true;
}

} // namespace language
