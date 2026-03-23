#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>

#include "Logger.h"
#include "CrashHandler.h"
#include "AppController.h"
#include "PluginManager.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("MyOrg");
    app.setApplicationName("VideoPlayer");
    app.setApplicationVersion("1.0.0");

    const QString dataDir = QStandardPaths::writableLocation(
                                QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir + "/logs");
    QDir().mkpath(dataDir + "/dumps");

    Logger::instance().init(
        (dataDir + "/logs").toStdString(),
        spdlog::level::debug
    );
    LOG_INFO("=== VideoPlayer starting (v1.0.0) ===");
    LOG_INFO("Data dir: {}", dataDir.toStdString());

    CrashHandler::install((dataDir + "/dumps").toStdString());
    QQuickStyle::setStyle("Basic");
    AppController::instance().initPlugins();

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& w : warnings)
                LOG_ERROR("[QML] {}", w.toString().toStdString());
        }
    );

    const QUrl entryUrl(QStringLiteral("qrc:/VideoPlayer/qml/main.qml"));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [entryUrl](QObject* obj, const QUrl& url) {
            if (!obj && url == entryUrl) {
                LOG_CRITICAL("Failed to load QML root: {}",
                             url.toString().toStdString());
                QCoreApplication::exit(1);
            }
        },
        Qt::QueuedConnection
    );

    engine.load(entryUrl);

    LOG_INFO("Entering event loop");
    int ret = app.exec();
    LOG_INFO("=== VideoPlayer exiting ({}) ===", ret);
    Logger::instance().shutdown();
    return ret;
}
