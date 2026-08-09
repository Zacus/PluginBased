#include "AppController.h"
#include "Logger.h"
#include "AppConfig.h"
#include "AppLanguageService.h"
#include "AppThemeService.h"
#include "BuildInfo.h"
#include "PluginPathResolver.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>

using PluginBased::App::AppConfig;

namespace PluginBased::App {

QString AppController::appVersion() const
{
    return BuildInfo::productVersion();
}

QString AppController::appName() const
{
    return BuildInfo::productName();
}

QString AppController::buildNumber() const
{
    return BuildInfo::buildNumber();
}

QString AppController::currentLanguage() const
{
    return AppLanguageService::instance().currentLanguage();
}

QStringList AppController::availableLanguages() const
{
    return AppLanguageService::instance().availableLanguages();
}

void AppController::initPlugins()
{
    LOG_INFO("AppController: initializing plugins...");
    setTheme(AppConfig::instance().themeName());

    const QString appDir = QCoreApplication::applicationDirPath();
    const PluginPathResolution pluginPath = PluginPathResolver::resolve(appDir);

    if (pluginPath.pluginDir.isEmpty()) {
        LOG_WARN("AppController: no plugin directory found (tried {} paths)",
                 pluginPath.candidateCount);
        LOG_WARN("AppController: appDir = {}", appDir.toStdString());
    } else {
        LOG_INFO("AppController: plugin dir = {}", pluginPath.pluginDir.toStdString());
        PluginManager::instance().loadAll(pluginPath.pluginDir);
        PluginManager::instance().applyLanguage(AppLanguageService::instance().currentLanguage());
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

void AppController::setTheme(const QString& themeName)
{
    const ThemeApplyResult result = AppThemeService::instance().applyTheme(themeName);

    if (m_currentTheme == result.appliedTheme)
        return;

    m_currentTheme = result.appliedTheme;
    emit currentThemeChanged();
}

void AppController::toggleTheme()
{
    setTheme(m_currentTheme == QStringLiteral("dark")
        ? QStringLiteral("light")
        : QStringLiteral("dark"));
}

bool AppController::setLanguage(const QString& languageName)
{
    const QString before = AppLanguageService::instance().currentLanguage();
    const bool ok = AppLanguageService::instance().applyLanguage(languageName);
    const QString after = AppLanguageService::instance().currentLanguage();
    if (before != after) {
        emit currentLanguageChanged();
        emit PluginManager::instance().pluginsChanged();
    }
    return ok;
}

void AppController::logInfo(const QString& msg)  { LOG_INFO("[QML] {}",  msg.toStdString()); }
void AppController::logWarn(const QString& msg)  { LOG_WARN("[QML] {}",  msg.toStdString()); }
void AppController::logError(const QString& msg) { LOG_ERROR("[QML] {}", msg.toStdString()); }

void AppController::copyBuildDiagnosticInfo()
{
    QGuiApplication::clipboard()->setText(BuildInfo::diagnosticSummary());
}

void AppController::reloadConfig()
{
    LOG_INFO("AppController: reloading config from {}", AppConfig::instance().path().toStdString());
    AppConfig::instance().load(AppConfig::instance().path());
    Logger::instance().setLevel(AppConfig::instance().logLevel());
    setTheme(AppConfig::instance().themeName());
    LOG_INFO("AppController: config reloaded successfully");
}

} // namespace PluginBased::App
