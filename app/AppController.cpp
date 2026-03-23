#include "AppController.h"
#include "Logger.h"
#include "PluginManager.h"

#include <QCoreApplication>

AppController::AppController(QObject* parent)
    : QObject(parent)
{}

AppController& AppController::instance()
{
    static AppController s_instance;
    return s_instance;
}

AppController* AppController::create(QQmlEngine* /*engine*/, QJSEngine* /*jsEngine*/)
{
    // QML 引擎不拥有此对象的所有权
    QQmlEngine::setObjectOwnership(&instance(), QQmlEngine::CppOwnership);
    return &instance();
}

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

void AppController::logInfo(const QString& msg)
{
    LOG_INFO("[QML] {}", msg.toStdString());
}

void AppController::logWarn(const QString& msg)
{
    LOG_WARN("[QML] {}", msg.toStdString());
}

void AppController::logError(const QString& msg)
{
    LOG_ERROR("[QML] {}", msg.toStdString());
}
