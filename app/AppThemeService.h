#pragma once

#include <QString>
#include <QStringList>

namespace PluginBased::App {

struct ThemeApplyResult
{
    QString requestedTheme;
    QString appliedTheme;
    QString themeDirectory;
    QString error;
    bool usedFallback = false;
};

class AppThemeService
{
public:
    static AppThemeService& instance();

    QStringList themeDirectoryCandidates() const;
    QString resolveThemeDirectory() const;
    void configure();
    ThemeApplyResult applyTheme(const QString& themeName);

private:
    AppThemeService() = default;

    bool m_configured = false;
    QString m_themeDirectory;
};

} // namespace PluginBased::App
