from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    config_h = read("app/AppConfig.h")
    config_cpp = read("app/AppConfig.cpp")
    root_cmake = read("CMakeLists.txt")

    require("QString languageName() const" in config_h, "AppConfig should expose languageName()")
    require("void setLanguageName(const QString& languageName)" in config_h, "AppConfig should expose setLanguageName()")
    require('m_languageName { "en_US" }' in config_h, "AppConfig should default language to en_US")
    require(
        'm_settings->setValue(QStringLiteral("language"), QStringLiteral("en_US"))' in config_cpp,
        "AppConfig should write default ui/language=en_US",
    )
    require(
        'setLanguageName(m_settings->value(QStringLiteral("language"), QStringLiteral("en_US")).toString())' in config_cpp,
        "AppConfig should load persisted ui/language",
    )
    require(
        'm_settings->setValue(QStringLiteral("language"), m_languageName)' in config_cpp,
        "AppConfig should save persisted ui/language",
    )
    require(
        '^[a-z]{2}_[A-Z]{2}$' in config_cpp,
        "AppConfig should validate language IDs as xx_YY",
    )
    require('QStringLiteral("en_US")' in config_cpp, "AppConfig should use en_US fallback")
    require("LinguistTools" in root_cmake, "root CMake should include Qt LinguistTools")

    service_h = read("app/AppLanguageService.h")
    service_cpp = read("app/AppLanguageService.cpp")
    app_cmake = read("app/CMakeLists.txt")

    require("class AppLanguageService" in service_h, "AppLanguageService should exist")
    require("QTranslator" in service_h, "AppLanguageService should own translators")
    require("QString currentLanguage() const" in service_h, "AppLanguageService should expose currentLanguage()")
    require("QStringList availableLanguages() const" in service_h, "AppLanguageService should expose availableLanguages()")
    require("bool applyLanguage(const QString& languageName)" in service_h, "AppLanguageService should apply language changes")
    require(
        "bool isSupportedLanguage(const QString& languageName) const" in service_h,
        "AppLanguageService should expose isSupportedLanguage()",
    )
    require("void languageChanged()" in service_h, "AppLanguageService should emit languageChanged()")
    require("QCoreApplication::installTranslator" in service_cpp, "AppLanguageService should install translators")
    require(
        "if (!QCoreApplication::installTranslator" in service_cpp,
        "AppLanguageService should handle translator installation failure",
    )
    require("QCoreApplication::removeTranslator" in service_cpp, "AppLanguageService should remove translators")
    require("AppConfig::instance().setLanguageName" in service_cpp, "AppLanguageService should persist selected language")
    require(
        "if (!isSupportedLanguage(requested))" in service_cpp,
        "AppLanguageService should warn when requested language is unsupported before normalization",
    )
    require(
        "AppConfig::instance().languageName() != normalized" in service_cpp,
        "AppLanguageService should persist normalized language even when current language is unchanged",
    )
    require("AppLanguageService.h" in app_cmake, "AppLanguageService header should be in app target")
    require("AppLanguageService.cpp" in app_cmake, "AppLanguageService source should be in app target")

    controller_h = read("app/AppController.h")
    controller_cpp = read("app/AppController.cpp")
    main_cpp = read("app/main.cpp")

    require(
        "Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY currentLanguageChanged)" in controller_h,
        "AppController should expose currentLanguage",
    )
    require(
        "Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)" in controller_h,
        "AppController should expose availableLanguages",
    )
    require(
        "Q_INVOKABLE bool setLanguage(const QString& languageName)" in controller_h,
        "AppController should expose setLanguage",
    )
    require(
        "AppLanguageService::instance().applyLanguage" in controller_cpp,
        "AppController should delegate language changes",
    )
    require("currentLanguageChanged" in controller_h, "AppController should emit currentLanguageChanged")
    require(
        "AppLanguageService::instance().applyLanguage(cfg.languageName())" in main_cpp,
        "main should apply configured language before QML loads",
    )
    require(
        "Logger::instance().init" in main_cpp
        and main_cpp.index("Logger::instance().init") < main_cpp.index("AppLanguageService::instance().applyLanguage(cfg.languageName())"),
        "main should initialize logging before applying language",
    )


if __name__ == "__main__":
    main()
