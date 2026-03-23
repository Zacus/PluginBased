#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>
#include <QtQml>

#include "Logger.h"
#include "CrashHandler.h"
#include "AppController.h"
#include "PlayerEngine.h"
#include "PlaylistModel.h"
#include "MediaInfo.h"
#include "PluginManager.h"

static void registerQmlTypes()
{
    const char* uri    = "VideoPlayer";
    const int   vmajor = 1;
    const int   vminor = 0;

    qmlRegisterSingletonType<AppController>(
        uri, vmajor, vminor, "AppController",
        &AppController::create
    );
    qmlRegisterSingletonType<PluginManager>(
        uri, vmajor, vminor, "PluginManager",
        &PluginManager::create
    );
    qmlRegisterType<PlayerEngine>(
        uri, vmajor, vminor, "PlayerEngine"
    );
    qmlRegisterType<PlaylistModel>(
        uri, vmajor, vminor, "PlaylistModel"
    );
    qmlRegisterUncreatableType<MediaInfo>(
        uri, vmajor, vminor, "MediaInfo",
        "MediaInfo is created by C++ only"
    );

    // ── 注册 QML 组件目录，让引擎能找到 IconButton / ProgressSlider ──────
    // qrc:/qml/         → 根目录，main.qml / PlayerView.qml 等顶层文件
    // qrc:/qml/components/ → IconButton.qml / ProgressSlider.qml
    qmlRegisterType(QUrl("qrc:/qml/components/IconButton.qml"),
                    "VideoPlayer.Components", 1, 0, "IconButton");
    qmlRegisterType(QUrl("qrc:/qml/components/ProgressSlider.qml"),
                    "VideoPlayer.Components", 1, 0, "ProgressSlider");
}

int main(int argc, char* argv[])
{
    // ── 1. QGuiApplication 最先
    QGuiApplication app(argc, argv);
    app.setOrganizationName("MyOrg");
    app.setApplicationName("VideoPlayer");
    app.setApplicationVersion("1.0.0");

    // ── 2. 系统可写目录（双击 .app 也能写）
    const QString dataDir = QStandardPaths::writableLocation(
                                QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDir + "/logs");
    QDir().mkpath(dataDir + "/dumps");

    // ── 3. 日志
    Logger::instance().init(
        (dataDir + "/logs").toStdString(),
        spdlog::level::debug
    );
    LOG_INFO("=== VideoPlayer starting (v1.0.0) ===");
    LOG_INFO("Data dir: {}", dataDir.toStdString());

    // ── 4. Dump
    CrashHandler::install((dataDir + "/dumps").toStdString());

    // ── 5. 样式
    QQuickStyle::setStyle("Basic");

    // ── 6. 注册 C++ 类型 + QML 组件
    registerQmlTypes();

    // ── 7. 初始化插件
    AppController::instance().initPlugins();

    // ── 8. QML 引擎
    QQmlApplicationEngine engine;

    // 把 QML 详细错误打到日志
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& w : warnings)
                LOG_ERROR("[QML] {}", w.toString().toStdString());
        }
    );

    // 把 qrc:/qml/ 加入 import 搜索路径，
    // 使得 QML 文件里可以直接写  IconButton { }  而无需 import 语句
    engine.addImportPath(QStringLiteral("qrc:/"));

    const QUrl entryUrl(QStringLiteral("qrc:/qml/main.qml"));
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
