#include "AppController.h"
#include "Logger.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>

void AppController::initPlugins()
{
    LOG_INFO("AppController: initializing plugins...");

    // 插件目录优先级：
    //  1. 可执行文件同级的 plugins/ 子目录（发布包结构）
    //  2. 可执行文件同级目录本身（macOS .app bundle 内）
    const QString appDir = QCoreApplication::applicationDirPath();
    QString pluginDir = appDir + "/plugins";

    if (!QDir(pluginDir).exists()) {
        // macOS .app bundle: Contents/MacOS/ -> 退回到 Contents/PlugIns/
        pluginDir = appDir + "/../PlugIns";
    }
    if (!QDir(pluginDir).exists()) {
        // 开发期构建目录：可执行文件在 app/ 下，插件在 ../plugins/
        pluginDir = appDir + "/../plugins";
    }

    LOG_INFO("AppController: plugin dir = {}", pluginDir.toStdString());
    PluginManager::instance().loadAll(pluginDir);

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
