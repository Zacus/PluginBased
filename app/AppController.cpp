#include "AppController.h"
#include "Logger.h"
#include "PluginManager.h"

#include <QCoreApplication>

void AppController::initPlugins()
{
    LOG_INFO("AppController: initializing plugins...");
    PluginManager::instance().loadAll("plugins");
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
