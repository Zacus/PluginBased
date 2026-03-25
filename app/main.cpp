#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QDir>

#include "Logger.h"
#include "CrashHandler.h"
#include "AppController.h"
#include "PluginManager.h"
#include "AppConfig.h"

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
    QDir().mkpath(dataDir + "/config");

    // ── 加载配置 ──────────────────────────────────────────────────────────
    AppConfig& cfg = AppConfig::instance();
    cfg.load(dataDir + "/config/videoplayer.ini");

    const QString logDir = QDir::isAbsolutePath(cfg.logDir())
                               ? cfg.logDir()
                               : dataDir + "/" + cfg.logDir();
    QDir().mkpath(logDir);

    Logger::instance().init(
        logDir.toStdString(),
        cfg.logLevel(),
        static_cast<std::size_t>(cfg.logMaxFileMB()) * 1024 * 1024,
        static_cast<std::size_t>(cfg.logMaxFiles()),
        cfg.logFlushOn()
    );
    LOG_INFO("=== VideoPlayer starting (v1.0.0) ===");
    LOG_INFO("Data dir: {}", dataDir.toStdString());
    LOG_INFO("Config  : {}", cfg.path().toStdString());

    CrashHandler::install((dataDir + "/dumps").toStdString());
    QQuickStyle::setStyle("Basic");

    // ── 插件加载（在 QQmlEngine 创建之前完成）────────────────────────────
    // PlayPlugin::initialize() 会调用 PlayerEngine::registerPluginFinder()，
    // 必须在 QML 引擎实例化任何 PlayerEngine 之前执行。
    AppController::instance().initPlugins();

    QQmlApplicationEngine engine;

    // ── QML 模块搜索路径 ──────────────────────────────────────────────────
    // CMAKE_BINARY_DIR 下同时存在：
    //   VideoPlayer/qmldir   ← VideoPlayer 1.0（宿主模块）
    //   PlayPlugin/qmldir    ← PlayPlugin 1.0（插件 C++ 类型模块）
    // 一次 addImportPath 即可让引擎发现两个模块。
#ifdef QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(QML_IMPORT_PATH));
#endif
    // qrc:/ 根路径：PlayPlugin.so 内嵌的 QML 文件通过 qrc 路径加载，
    // 无需额外 addImportPath，Loader { source: "qrc:/..." } 直接可用。

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
