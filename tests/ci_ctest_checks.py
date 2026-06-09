#!/usr/bin/env python3

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    root_cmake = read("CMakeLists.txt")
    workflow_path = ROOT / ".github" / "workflows" / "ci.yml"
    plugin_manifest_path = ROOT / "plugins.json"

    require("include(CTest)" in root_cmake,
            "top-level CMake should include CTest")
    require("GuiPrivate" in root_cmake,
            "top-level CMake should explicitly find Qt GuiPrivate because PlayPlugin links Qt6::GuiPrivate")
    require("BUILD_TESTING" in root_cmake,
            "top-level CMake should gate tests behind BUILD_TESTING")
    require("add_test(NAME playplugin_regression_checks" in root_cmake,
            "CTest should run playplugin_regression_checks.py")
    require("add_test(NAME plugin_generator_checks" in root_cmake,
            "CTest should run plugin_generator_checks.py")
    require("add_test(NAME ci_ctest_checks" in root_cmake,
            "CTest should run ci_ctest_checks.py")
    require("PluginGeneratorBackendSmokeTest" in root_cmake,
            "CTest should expose the generator backend smoke target")

    require(plugin_manifest_path.exists(),
            "plugins.json should control which plugins participate in the build")
    plugin_manifest = json.loads(plugin_manifest_path.read_text(encoding="utf-8"))
    require(plugin_manifest.get("plugins") == ["DummyPlugin", "PlayPlugin"],
            "plugin manifest should list only the default build plugins in order")
    require("demoPlugin" not in plugin_manifest.get("plugins", []),
            "local generated plugins should not be built unless explicitly listed")
    require("string(JSON" in root_cmake and "PLUGIN_MANIFEST" in root_cmake,
            "top-level CMake should read the plugin manifest as JSON")
    require("file(GLOB PLUGIN_CMAKELISTS" not in root_cmake,
            "top-level CMake should not auto-scan every plugin CMakeLists.txt")
    require("Plugin directory listed in manifest does not exist" in root_cmake,
            "top-level CMake should fail fast when a listed plugin directory is missing")
    require("Plugin listed in manifest has no CMakeLists.txt" in root_cmake,
            "top-level CMake should fail fast when a listed plugin has no CMakeLists.txt")
    require("Invalid plugin name in manifest" in root_cmake,
            "top-level CMake should reject unsafe plugin names")
    require("configure_file(${PLUGIN_MANIFEST}" in root_cmake,
            "top-level CMake should copy the plugin manifest to the build root")
    require('"${CMAKE_BINARY_DIR}/plugins.json"' in root_cmake,
            "top-level CMake should place the runtime plugin manifest in the build root")
    require('install(FILES "${PLUGIN_MANIFEST}" DESTINATION ".")' in root_cmake,
            "top-level CMake should install the plugin manifest to the package root")

    plugin_manager_cpp = read("core/PluginManager.cpp")
    require("plugins.json" in plugin_manager_cpp and "QJsonDocument" in plugin_manager_cpp,
            "PluginManager should read plugins.json at runtime")
    require("manifestFilePath" in plugin_manager_cpp,
            "PluginManager should resolve the manifest path from the plugin directory")
    require("PluginManager: plugin manifest not found" in plugin_manager_cpp,
            "PluginManager should not fall back to loading all plugins when manifest is missing")
    require("manifestPluginNames" in plugin_manager_cpp,
            "PluginManager should load only plugin names listed by the manifest")

    app_config_h = read("app/AppConfig.h")
    app_config_cpp = read("app/AppConfig.cpp")
    require("themeName() const" in app_config_h,
            "AppConfig should expose the persisted UI theme")
    require("setThemeName" in app_config_h,
            "AppConfig should allow updating the persisted UI theme")
    require('beginGroup(QStringLiteral("ui"))' in app_config_cpp,
            "AppConfig should persist UI settings in [ui]")
    require('QStringLiteral("theme")' in app_config_cpp,
            "AppConfig should read and write the ui/theme key")

    app_controller_h = read("app/AppController.h")
    app_controller_cpp = read("app/AppController.cpp")
    app_theme_service_h = read("app/AppThemeService.h")
    app_theme_service_cpp = read("app/AppThemeService.cpp")
    root_cmake = read("CMakeLists.txt")
    readme = read("README.md")
    require("currentTheme READ currentTheme" in app_controller_h,
            "AppController should expose currentTheme to QML")
    require("setTheme(" in app_controller_h,
            "AppController should expose setTheme to QML")
    require("toggleTheme" in app_controller_h,
            "AppController should expose toggleTheme to QML")
    require("class AppThemeService" in app_theme_service_h,
            "Theme orchestration should live in AppThemeService")
    require("ThemeApplyResult" in app_theme_service_h,
            "Theme application should return a structured result")
    require("ComponentTheme::instance().loadTheme" in app_theme_service_cpp,
            "AppThemeService should apply theme changes through JSON theme ids")
    require("usedFallback" in app_theme_service_cpp and "lastError" in app_theme_service_cpp,
            "AppThemeService should handle failed theme loads with a structured fallback result")
    require("ComponentTheme::instance().loadTheme" not in app_controller_cpp,
            "AppController should delegate theme application to AppThemeService")
    require("PLUGINBASED_THEME_OUTPUT_DIR" in root_cmake,
            "CMake should define how theme JSON files are copied for runtime")
    require("Contents/Resources/themes" in app_theme_service_cpp,
            "AppThemeService should support macOS bundle Resources/themes")
    require("<app>/themes" in readme,
            "README should document runtime theme directory contract")

    main_qml = read("app/qml/main.qml")
    require("AppController.toggleTheme()" in main_qml,
            "main toolbar should expose a theme toggle action")
    require("AppController.currentTheme" in main_qml,
            "main toolbar should bind theme toggle state to AppController.currentTheme")

    home_panel_qml = read("app/qml/HomePanel.qml")
    require("ComponentTheme.surface" in main_qml,
            "main.qml should use ComponentTheme surface tokens")
    require("ComponentTheme.textPrimary" in main_qml,
            "main.qml should use ComponentTheme text tokens")
    require("import QuickUI.Components 1.0" in home_panel_qml,
            "HomePanel should import QuickUI.Components")
    require("ComponentTheme.surface" in home_panel_qml,
            "HomePanel should use ComponentTheme surface tokens")
    require("ComponentTheme.textPrimary" in home_panel_qml,
            "HomePanel should use ComponentTheme text tokens")

    require(workflow_path.exists(), "GitHub Actions CI workflow should exist")
    workflow = workflow_path.read_text(encoding="utf-8")
    require("ctest --test-dir build" in workflow,
            "CI should run CTest")
    require("cmake --build build --parallel" in workflow,
            "CI should build the project")
    require("QtQuickComponents" in workflow,
            "CI should checkout or provide QtQuickComponents")
    require("CMAKE_TOOLCHAIN_FILE" in workflow,
            "CI should configure with vcpkg manifest dependencies")


if __name__ == "__main__":
    main()
