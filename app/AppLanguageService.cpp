#include "AppLanguageService.h"

#include "AppConfig.h"
#include "Logger.h"

#include <QCoreApplication>

namespace PluginBased::App {

namespace {

constexpr const char* kDefaultLanguage = "en_US";
constexpr const char* kChineseLanguage = "zh_CN";

} // namespace

AppLanguageService::AppLanguageService(QObject* parent)
    : QObject(parent)
{}

QStringList AppLanguageService::availableLanguages() const
{
    return {
        QStringLiteral("en_US"),
        QStringLiteral("zh_CN"),
    };
}

bool AppLanguageService::isSupportedLanguage(const QString& languageName) const
{
    return availableLanguages().contains(languageName);
}

bool AppLanguageService::applyLanguage(const QString& languageName)
{
    const QString requested = languageName.trimmed().isEmpty()
        ? QString::fromLatin1(kDefaultLanguage)
        : languageName.trimmed();
    if (!isSupportedLanguage(requested))
        LOG_WARN("AppLanguageService: unsupported requested language '{}'", requested.toStdString());

    const QString normalized = isSupportedLanguage(requested)
        ? requested
        : QString::fromLatin1(kDefaultLanguage);

    if (m_currentLanguage == normalized) {
        if (AppConfig::instance().languageName() != normalized) {
            AppConfig::instance().setLanguageName(normalized);
            AppConfig::instance().save();
        }
        return true;
    }

    removeInstalledTranslators();

    if (!installTranslationFor(normalized)) {
        m_currentLanguage = QString::fromLatin1(kDefaultLanguage);
        AppConfig::instance().setLanguageName(m_currentLanguage);
        AppConfig::instance().save();
        emit languageChanged();
        return normalized == QString::fromLatin1(kDefaultLanguage);
    }

    m_currentLanguage = normalized;
    AppConfig::instance().setLanguageName(m_currentLanguage);
    AppConfig::instance().save();
    emit languageChanged();
    return true;
}

void AppLanguageService::removeInstalledTranslators()
{
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator.get());
        m_appTranslator.reset();
    }
}

bool AppLanguageService::installTranslationFor(const QString& languageName)
{
    if (languageName == QString::fromLatin1(kDefaultLanguage))
        return true;

    if (languageName != QString::fromLatin1(kChineseLanguage)) {
        LOG_WARN("AppLanguageService: unsupported language '{}'", languageName.toStdString());
        return false;
    }

    auto translator = std::make_unique<QTranslator>();
    const QString resourcePath = QStringLiteral(":/i18n/pluginbased_zh_CN.qm");
    if (!translator->load(resourcePath)) {
        LOG_WARN("AppLanguageService: failed to load {}", resourcePath.toStdString());
        return false;
    }

    QCoreApplication::installTranslator(translator.get());
    m_appTranslator = std::move(translator);
    return true;
}

} // namespace PluginBased::App
