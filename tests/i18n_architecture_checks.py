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
    plugin_interface_h = read("plugin/IAppPlugin.h")
    plugin_manager_h = read("core/PluginManager.h")
    plugin_manager_cpp = read("core/PluginManager.cpp")

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
    require(
        "engine.retranslate()" in main_cpp,
        "main should trigger QQmlEngine::retranslate() after runtime language changes",
    )
    require(
        "&AppLanguageService::languageChanged" in main_cpp,
        "main should connect AppLanguageService languageChanged to QML retranslation",
    )
    require(
        "PluginManager::instance().applyLanguage(AppLanguageService::instance().currentLanguage())" in main_cpp
        and main_cpp.index("PluginManager::instance().applyLanguage(AppLanguageService::instance().currentLanguage())")
        < main_cpp.index("engine.retranslate()"),
        "main should install plugin translators before QML retranslation on runtime language changes",
    )
    require(
        "PluginManager::instance().unloadAll();" in main_cpp,
        "main should explicitly unload plugins before QCoreApplication destruction",
    )
    require(
        main_cpp.index("ret = app.exec();") < main_cpp.index("PluginManager::instance().unloadAll();")
        < main_cpp.index("Logger::instance().shutdown();"),
        "main should unload plugins after the event loop exits and before logger/QCoreApplication teardown",
    )
    require(
        "    {\n        QQmlApplicationEngine engine;" in main_cpp
        and main_cpp.index("    {\n        QQmlApplicationEngine engine;")
        < main_cpp.index("PluginManager::instance().unloadAll();"),
        "main should destroy QQmlApplicationEngine before unloading plugin libraries",
    )
    require(
        "PluginManager::instance().applyLanguage(AppLanguageService::instance().currentLanguage())" in controller_cpp,
        "AppController should apply plugin translators after initial plugin loading",
    )
    require(
        "virtual QStringList translationResourcePaths(const QString& languageName) const" in plugin_interface_h,
        "IAppPlugin should expose plugin-owned translation resource paths",
    )
    require(
        "PluginBasedPluginAbiVersion = 2" in plugin_interface_h,
        "IAppPlugin ABI version should be bumped when adding a virtual method",
    )
    require("QTranslator" in plugin_manager_h, "PluginManager should own plugin translators")
    require(
        "void applyLanguage(const QString& languageName)" in plugin_manager_h,
        "PluginManager should expose plugin translator application",
    )
    require(
        "removeInstalledTranslators" in plugin_manager_h and "removeInstalledTranslators" in plugin_manager_cpp,
        "PluginManager should remove installed plugin translators before replacement or unload",
    )
    require(
        "translationResourcePaths(languageName)" in plugin_manager_cpp,
        "PluginManager should query each plugin for language-specific translation resources",
    )
    require(
        "QCoreApplication::installTranslator" in plugin_manager_cpp
        and "QCoreApplication::removeTranslator" in plugin_manager_cpp,
        "PluginManager should install and remove plugin translators through QCoreApplication",
    )
    require(
        "m_plugins.empty() && m_pluginTranslators.empty()" in plugin_manager_cpp,
        "PluginManager unloadAll should be idempotent after explicit shutdown",
    )

    translation_ts = read("translations/pluginbased_zh_CN.ts")

    require("qt_add_translations" in app_cmake, "app CMake should generate translation resources")
    require("pluginbased_zh_CN.ts" in app_cmake, "app CMake should list the Chinese TS file")
    require('<TS version="2.1" language="zh_CN">' in translation_ts, "Chinese TS should declare zh_CN")
    require("<source>Application Panel</source>" in translation_ts, "Chinese TS should include host homepage text")
    require("<name>PlayPlugin</name>" not in translation_ts, "Host Chinese TS should not own PlayPlugin C++ translations")
    require("<name>PlayPluginView</name>" not in translation_ts, "Host Chinese TS should not own PlayPlugin QML translations")
    require("<name>DummyPlugin</name>" not in translation_ts, "Host Chinese TS should not own DummyPlugin C++ translations")
    require("<name>DummyPluginView</name>" not in translation_ts, "Host Chinese TS should not own DummyPlugin QML translations")

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
    play_cpp = read("plugins/PlayPlugin/PlayPlugin.cpp")
    play_cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    play_ts = read("plugins/PlayPlugin/translations/PlayPlugin_zh_CN.ts")
    play_qml = read("plugins/PlayPlugin/qml/PlayPluginView.qml")
    player_view_qml = read("plugins/PlayPlugin/qml/PlayerView.qml")
    playlist_view_qml = read("plugins/PlayPlugin/qml/PlaylistView.qml")
    control_bar_qml = read("plugins/PlayPlugin/qml/ControlBar.qml")
    dummy_h = read("plugins/DummyPlugin/DummyPlugin.h")
    dummy_cpp = read("plugins/DummyPlugin/DummyPlugin.cpp")
    dummy_cmake = read("plugins/DummyPlugin/CMakeLists.txt")
    dummy_ts = read("plugins/DummyPlugin/translations/DummyPlugin_zh_CN.ts")
    dummy_qml = read("plugins/DummyPlugin/qml/DummyPluginView.qml")

    require('tr("Built-in player: video + playlist")' in play_h, "PlayPlugin description should be translatable")
    require('tr("Video Player")' in play_h, "PlayPlugin card name should be translatable")
    require("translationResourcePaths" in play_h, "PlayPlugin should override translation resource paths")
    require(":/PlayPlugin/i18n/PlayPlugin_zh_CN.qm" in play_cpp, "PlayPlugin should return its own Chinese qm resource")
    require("qt_add_translations(PlayPlugin" in play_cmake, "PlayPlugin CMake should generate plugin translations")
    require("PlayPlugin_zh_CN.ts" in play_cmake, "PlayPlugin CMake should list its Chinese TS file")
    require('RESOURCE_PREFIX "/PlayPlugin/i18n"' in play_cmake, "PlayPlugin translations should use plugin resource namespace")
    require("<name>PlayPlugin</name>" in play_ts, "PlayPlugin TS should include C++ plugin context")
    require("<name>ControlBar</name>" in play_ts, "PlayPlugin TS should include ControlBar context")
    require('qsTr("Video Player")' in play_qml, "PlayPlugin QML title should be translatable")
    for plugin_qml_path, plugin_qml_text in (
        ("plugins/PlayPlugin/qml/PlayPluginView.qml", play_qml),
        ("plugins/PlayPlugin/qml/PlayerView.qml", player_view_qml),
        ("plugins/PlayPlugin/qml/PlaylistView.qml", playlist_view_qml),
        ("plugins/PlayPlugin/qml/ControlBar.qml", control_bar_qml),
        ("plugins/DummyPlugin/qml/DummyPluginView.qml", dummy_qml),
    ):
        require(
            "import PluginBased 1.0" not in plugin_qml_text,
            f"{plugin_qml_path} should not depend on host PluginBased module for i18n refresh",
        )
        require(
            "AppController.currentLanguage" not in plugin_qml_text,
            f"{plugin_qml_path} should rely on QQmlEngine::retranslate() instead of host language bindings",
        )
    require(
        'title: qsTr("Open Media File")' in player_view_qml,
        "PlayerView file dialog title should use plain qsTr",
    )
    require(
        'text: qsTr("Open a media file to start playback")' in player_view_qml,
        "PlayerView empty-state text should use plain qsTr",
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
    require("translationResourcePaths" in dummy_h, "DummyPlugin should override translation resource paths")
    require(":/DummyPlugin/i18n/DummyPlugin_zh_CN.qm" in dummy_cpp, "DummyPlugin should return its own Chinese qm resource")
    require("qt_add_translations(DummyPlugin" in dummy_cmake, "DummyPlugin CMake should generate plugin translations")
    require("DummyPlugin_zh_CN.ts" in dummy_cmake, "DummyPlugin CMake should list its Chinese TS file")
    require('RESOURCE_PREFIX "/DummyPlugin/i18n"' in dummy_cmake, "DummyPlugin translations should use plugin resource namespace")
    require("<name>DummyPlugin</name>" in dummy_ts, "DummyPlugin TS should include C++ plugin context")
    require("<name>DummyPluginView</name>" in dummy_ts, "DummyPlugin TS should include QML context")
    require("qsTr(" in dummy_qml, "DummyPlugin QML should use qsTr for visible text")


if __name__ == "__main__":
    main()
