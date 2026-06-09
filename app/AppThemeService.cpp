#include "AppThemeService.h"

#include "AppConfig.h"
#include "ComponentTheme.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QDir>

namespace PluginBased::App {

AppThemeService& AppThemeService::instance()
{
    static AppThemeService service;
    return service;
}

QStringList AppThemeService::themeDirectoryCandidates() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        appDir + "/themes",
        appDir + "/../Resources/themes", // macOS: PluginBasedApp.app/Contents/Resources/themes
        appDir + "/../themes",
        appDir + "/../../../../themes",
    };
}

QString AppThemeService::resolveThemeDirectory() const
{
    const QStringList candidates = themeDirectoryCandidates();
    for (const QString& candidate : candidates) {
        const QString clean = QDir::cleanPath(candidate);
        if (QDir(clean).exists())
            return clean;
    }

    return QDir::cleanPath(candidates.first());
}

void AppThemeService::configure()
{
    if (m_configured)
        return;

    m_themeDirectory = resolveThemeDirectory();
    ComponentTheme::instance().setThemeDirectory(m_themeDirectory);
    ComponentTheme::instance().setHotReloadEnabled(true);
    LOG_INFO("AppThemeService: theme dir = {}", m_themeDirectory.toStdString());
    m_configured = true;
}

ThemeApplyResult AppThemeService::applyTheme(const QString& themeName)
{
    configure();

    ThemeApplyResult result;
    result.requestedTheme = themeName;
    result.themeDirectory = m_themeDirectory;

    AppConfig::instance().setThemeName(themeName);
    QString next = AppConfig::instance().themeName();

    if (!ComponentTheme::instance().loadTheme(next)) {
        result.usedFallback = true;
        result.error = ComponentTheme::instance().lastError();
        LOG_WARN("AppThemeService: failed to load theme '{}': {}",
                 next.toStdString(),
                 result.error.toStdString());
        next = QStringLiteral("dark");
        AppConfig::instance().setThemeName(next);
        ComponentTheme::instance().loadTheme(next);
    }

    AppConfig::instance().save();
    result.appliedTheme = next;
    return result;
}

} // namespace PluginBased::App
