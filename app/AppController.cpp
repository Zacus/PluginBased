#include "AppController.h"
#include "Logger.h"
#include "AppConfig.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>

// void AppController::initPlugins()
// {
//     LOG_INFO("AppController: initializing plugins...");

//     // 插件目录优先级：
//     //  1. 可执行文件同级的 plugins/ 子目录（发布包结构）
//     //  2. 可执行文件同级目录本身（macOS .app bundle 内）
//     const QString appDir = QCoreApplication::applicationDirPath();
//     QString pluginDir = appDir + "/plugins";

//     if (!QDir(pluginDir).exists()) {
//         // macOS .app bundle: Contents/MacOS/ -> 退回到 Contents/PlugIns/
//         pluginDir = appDir + "/../PlugIns";
//     }
//     if (!QDir(pluginDir).exists()) {
//         // 开发期构建目录：可执行文件在 app/ 下，插件在 ../plugins/
//         pluginDir = appDir + "/../plugins";
//     }

//     LOG_INFO("AppController: plugin dir = {}", pluginDir.toStdString());
//     PluginManager::instance().loadAll(pluginDir);

//     m_pluginsReady = true;
//     emit pluginsReadyChanged();
//     LOG_INFO("AppController: plugins ready, count = {}",
//              PluginManager::instance().pluginCount());
// }
void AppController::initPlugins()
{
    LOG_INFO("AppController: initializing plugins...");
 
    // 插件目录探测顺序（从最具体到最通用）：
    //
    //  1. <appDir>/plugins/
    //     → 发布包：bin/PluginBasedApp  +  bin/plugins/*.so
    //
    //  2. <appDir>/../PlugIns/
    //     → macOS .app 发布包：Contents/MacOS/  +  Contents/PlugIns/
    //     注意：Qt 会在开发构建时自动创建此目录但内容为空，不能仅凭目录存在判断
    //
    //  3. <appDir>/../plugins/
    //     → 普通 CMake 开发构建（Linux/Win）：
    //       build/app/PluginBasedApp  +  build/plugins/
    //
    //  4. <appDir>/../../../../plugins/
    //     → macOS CMake 开发构建（.app bundle）：
    //       build/app/PluginBasedApp.app/Contents/MacOS/  +  build/plugins/
    //       需要上溯 4 级才能到 build/ 再进入 plugins/
 
    const QString appDir = QCoreApplication::applicationDirPath();
 
    const QStringList candidates = {
        appDir + "/plugins",
        appDir + "/../PlugIns",
        appDir + "/../plugins",
        appDir + "/../../../../plugins",  // macOS .app bundle 开发构建
    };
 
#if defined(Q_OS_WIN)
    const QStringList filters{"*.dll"};
#else
    const QStringList filters{"*.so"};
#endif
 
    QString pluginDir;
    for (const QString& candidate : candidates) {
        const QString clean = QDir::cleanPath(candidate);
        // 目录必须存在且含有至少一个插件文件，避免命中 Qt 自动创建的空 PlugIns 目录
        QDir dir(clean);
        if (dir.exists() && !dir.entryList(filters, QDir::Files).isEmpty()) {
            pluginDir = clean;
            break;
        }
        if (dir.exists()) {
            LOG_DEBUG("AppController: candidate exists but empty, skipping: {}",
                      clean.toStdString());
        }
    }
 
    if (pluginDir.isEmpty()) {
        LOG_WARN("AppController: no plugin directory found (tried {} paths)",
                 candidates.size());
        LOG_WARN("AppController: appDir = {}", appDir.toStdString());
    } else {
        LOG_INFO("AppController: plugin dir = {}", pluginDir.toStdString());
        PluginManager::instance().loadAll(pluginDir);
    }
 
    m_pluginsReady = true;
    emit pluginsReadyChanged();
    LOG_INFO("AppController: plugins ready, count = {}",
             PluginManager::instance().pluginCount());
}

void AppController::quit()
{
    LOG_INFO("AppController: quit requested");
    QCoreApplication::quit();
}

void AppController::logInfo(const QString& msg)  { LOG_INFO("[QML] {}",  msg.toStdString()); }
void AppController::logWarn(const QString& msg)  { LOG_WARN("[QML] {}",  msg.toStdString()); }
void AppController::logError(const QString& msg) { LOG_ERROR("[QML] {}", msg.toStdString()); }

void AppController::reloadConfig()
{
    LOG_INFO("AppController: reloading config from {}", AppConfig::instance().path().toStdString());
    AppConfig::instance().load(AppConfig::instance().path());
    Logger::instance().setLevel(AppConfig::instance().logLevel());
    LOG_INFO("AppController: config reloaded successfully");
}
