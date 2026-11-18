#include "language.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("SkeletonSkelettron"));
    QGuiApplication::setApplicationName(QStringLiteral("obdreadr"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("OBD Reader"));

    QQuickStyle::setStyle(QStringLiteral("Material"));
    language::apply(language::setting());

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("ObdReadr", "Main");

    return app.exec();
}
