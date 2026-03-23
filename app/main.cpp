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

// 注：静态构建时不需要手写 Q_IMPORT_QML_PLUGIN。
// qt_add_executable 在 finalization 阶段会自动调用 qmlimportscanner
// 扫描 QML 文件并生成 <target>_qml_plugin_import.cpp，
// 其中包含所有静态 QML 插件的 Q_IMPORT_PLUGIN 调用。

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

    // ── 加载配置（Logger 初始化前，确保 log 参数已就绪） ──
    AppConfig& cfg = AppConfig::instance();
    cfg.load(dataDir + "/config/videoplayer.ini");

    // 若 logDir 为相对路径，则以 dataDir 为基准
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
    AppController::instance().initPlugins();

    QQmlApplicationEngine engine;

#ifdef QML_IMPORT_PATH
    engine.addImportPath(QStringLiteral(QML_IMPORT_PATH));
#endif

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
