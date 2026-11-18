#include "background.h"

#ifdef Q_OS_ANDROID

#include <QCoreApplication>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace background {

namespace {

constexpr char kClass[] = "io/github/skeletonskelettron/obdreadr/ObdBackground";

QJniObject context()
{
    return QNativeInterface::QAndroidApplication::context();
}

void notify(const char *method, const QString &title, const QString &text)
{
    const QJniObject ctx = context();
    if (!ctx.isValid())
        return;
    const QJniObject jTitle = QJniObject::fromString(title);
    const QJniObject jText = QJniObject::fromString(text);
    QJniObject::callStaticMethod<void>(kClass, method,
                                       "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
                                       ctx.object(), jTitle.object<jstring>(), jText.object<jstring>());
}

} // namespace

bool supported()
{
    return true;
}

void start(const QString &title, const QString &text)
{
    notify("start", title, text);
}

void update(const QString &title, const QString &text)
{
    notify("update", title, text);
}

void stop()
{
    const QJniObject ctx = context();
    if (ctx.isValid())
        QJniObject::callStaticMethod<void>(kClass, "stop", "(Landroid/content/Context;)V", ctx.object());
}

void alert(const QString &title, const QString &text)
{
    notify("alert", title, text);
}

void keepScreenOn(bool on)
{
    const QJniObject ctx = context();
    if (ctx.isValid())
        QJniObject::callStaticMethod<void>(kClass, "keepScreenOn", "(Landroid/content/Context;Z)V", ctx.object(),
                                           jboolean(on));
}

bool batteryRestricted()
{
    const QJniObject ctx = context();
    return ctx.isValid()
           && QJniObject::callStaticMethod<jboolean>(kClass, "batteryRestricted", "(Landroid/content/Context;)Z",
                                                     ctx.object());
}

void requestUnrestricted()
{
    const QJniObject ctx = context();
    if (ctx.isValid())
        QJniObject::callStaticMethod<void>(kClass, "requestUnrestricted", "(Landroid/content/Context;)V", ctx.object());
}

} // namespace background

#else

namespace background {

bool supported() { return false; }
void start(const QString &, const QString &) {}
void update(const QString &, const QString &) {}
void stop() {}
void alert(const QString &, const QString &) {}
void keepScreenOn(bool) {}
bool batteryRestricted() { return false; }
void requestUnrestricted() {}

} // namespace background

#endif
