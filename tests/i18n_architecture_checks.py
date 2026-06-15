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

    translation_ts = read("translations/pluginbased_zh_CN.ts")

    require("qt_add_translations" in app_cmake, "app CMake should generate translation resources")
    require("pluginbased_zh_CN.ts" in app_cmake, "app CMake should list the Chinese TS file")
    require('<TS version="2.1" language="zh_CN">' in translation_ts, "Chinese TS should declare zh_CN")
    require("<source>Application Panel</source>" in translation_ts, "Chinese TS should include host homepage text")
    require("<source>Video Player</source>" in translation_ts, "Chinese TS should include PlayPlugin text")

    main_qml = read("app/qml/main.qml")
    home_qml = read("app/qml/HomePanel.qml")

    require('qsTr("Back to Home")' in main_qml, "main.qml should translate Back to Home")
    require('qsTr("Loading plugin UI...")' in main_qml, "main.qml should translate loading plugin UI text")
    require("languageSelector" in main_qml, "main.qml should include a language selector")
    require("AppController.setLanguage" in main_qml, "language selector should call AppController.setLanguage")
    require(
        "property int" in main_qml and "pluginIndex" in main_qml,
        "plugin page wrapper should retain plugin index for language refresh",
    )
    require(
        "PluginManager.pluginCardName(pluginIndex)" in main_qml,
        "plugin page title should re-read plugin card name when language changes",
    )
    require(
        '"pluginIndex":   pluginIndex' in main_qml,
        "plugin page push should pass plugin index into the wrapper",
    )
    require('qsTr("Application Panel")' in home_qml, "HomePanel should translate title")
    require('qsTr("Installed Plugins")' in home_qml, "HomePanel should translate plugin section")
    require('qsTr("Install More Plugins")' in home_qml, "HomePanel should translate install-more card")
    require(
        "cardName: AppController.currentLanguage, PluginManager.pluginCardName(index)" in home_qml,
        "plugin card names should depend on currentLanguage so they refresh at runtime",
    )
    require(
        "cardDesc: AppController.currentLanguage, PluginManager.pluginDescriptionAt(index)" in home_qml,
        "plugin card descriptions should depend on currentLanguage so they refresh at runtime",
    )

    play_h = read("plugins/PlayPlugin/PlayPlugin.h")
    play_qml = read("plugins/PlayPlugin/qml/PlayPluginView.qml")
    player_view_qml = read("plugins/PlayPlugin/qml/PlayerView.qml")
    playlist_view_qml = read("plugins/PlayPlugin/qml/PlaylistView.qml")
    control_bar_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    dummy_h = read("plugins/DummyPlugin/DummyPlugin.h")
    dummy_qml = read("plugins/DummyPlugin/qml/DummyPluginView.qml")

    require('tr("Built-in player: video + playlist")' in play_h, "PlayPlugin description should be translatable")
    require('tr("Video Player")' in play_h, "PlayPlugin card name should be translatable")
    require('qsTr("Video Player")' in play_qml, "PlayPlugin QML title should be translatable")
    require("import PluginBased 1.0" in player_view_qml, "PlayerView should access AppController for language refresh")
    require("import PluginBased 1.0" in playlist_view_qml, "PlaylistView should access AppController for language refresh")
    require("import PluginBased 1.0" in control_bar_qml, "ControlBar should access AppController for language refresh")
    require(
        'title: AppController.currentLanguage, qsTr("Open Media File")' in player_view_qml,
        "PlayerView file dialog title should refresh when language changes",
    )
    require(
        'text: AppController.currentLanguage, qsTr("Open a media file to start playback")' in player_view_qml,
        "PlayerView empty-state text should refresh when language changes",
    )
    require('qsTr("Playlist")' in playlist_view_qml, "PlaylistView title should be translatable")
    require('qsTr("%1 item(s)")' in playlist_view_qml, "PlaylistView item count should be translatable")
    require(
        'qsTr("No playlist items\\nClick + in the lower left to add media files")' in playlist_view_qml,
        "PlaylistView empty-state text should be translatable",
    )
    require(
        'qsTr("Double-click to play, hover to remove")' in playlist_view_qml,
        "PlaylistView footer hint should be translatable",
    )
    require('qsTr("Open file")' in control_bar_qml, "ControlBar open tooltip should be translatable")
    require('qsTr("Hide playlist")' in control_bar_qml, "ControlBar playlist tooltip should be translatable")
    require('tr("Example Plugin")' in dummy_h, "DummyPlugin card name should be translatable")
    require('tr("Stub plugin for framework validation")' in dummy_h, "DummyPlugin description should be translatable")
    require("qsTr(" in dummy_qml, "DummyPlugin QML should use qsTr for visible text")
    require("<name>PlayPluginView</name>" in translation_ts, "Chinese TS should include PlayPlugin QML context")
    require("<name>PlayerView</name>" in translation_ts, "Chinese TS should include PlayerView QML context")
    require("<source>Open a media file to start playback</source>" in translation_ts, "Chinese TS should include player empty-state text")
    require("<name>PlaylistView</name>" in translation_ts, "Chinese TS should include PlaylistView QML context")
    require("<source>Playlist</source>" in translation_ts, "Chinese TS should include playlist title")
    require("<name>ControlBar</name>" in translation_ts, "Chinese TS should include ControlBar QML context")
    require("<source>Open file</source>" in translation_ts, "Chinese TS should include control tooltip text")
    require("<name>DummyPluginView</name>" in translation_ts, "Chinese TS should include DummyPlugin QML context")


if __name__ == "__main__":
    main()
