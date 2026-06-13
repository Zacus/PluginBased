#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <memory>

namespace PluginBased::App {

class AppLanguageService : public QObject
{
    Q_OBJECT

public:
    static AppLanguageService& instance()
    {
        static AppLanguageService s;
        return s;
    }

    QString currentLanguage() const { return m_currentLanguage; }
    QStringList availableLanguages() const;
    bool applyLanguage(const QString& languageName);
    bool isSupportedLanguage(const QString& languageName) const;

signals:
    void languageChanged();

private:
    explicit AppLanguageService(QObject* parent = nullptr);

    void removeInstalledTranslators();
    bool installTranslationFor(const QString& languageName);

    QString m_currentLanguage { "en_US" };
    std::unique_ptr<QTranslator> m_appTranslator;
};

} // namespace PluginBased::App
