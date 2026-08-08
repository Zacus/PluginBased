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
    expected_vcpkg_baseline = "ea1a7396b05637a53bf23c078647ecc0edee4b80"
    expected_vcpkg_overrides = {
        "spdlog": "1.17.0",
        "ffmpeg": "8.0.1#2",
        "pkgconf": "2.5.1#4",
    }

    presets_path = ROOT / "CMakePresets.json"
    require(presets_path.exists(),
            "CMakePresets.json should define the supported build interface")
    presets = json.loads(presets_path.read_text(encoding="utf-8"))
    require(presets.get("version") == 3,
            "CMake presets should use schema version 3 for CMake 3.21")
    require(presets.get("cmakeMinimumRequired") == {
                "major": 3, "minor": 21, "patch": 0
            },
            "CMake presets should match the project CMake 3.21 floor")

    configure_presets = {
        preset.get("name"): preset
        for preset in presets.get("configurePresets", [])
    }
    base_preset = configure_presets.get("base", {})
    require(base_preset.get("hidden") is True,
            "CMake presets should keep shared configuration in a hidden base preset")
    require(base_preset.get("toolchainFile") ==
            "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
            "CMake presets should obtain the vcpkg toolchain from VCPKG_ROOT")
    base_cache = base_preset.get("cacheVariables", {})
    require(base_cache.get("CMAKE_PREFIX_PATH", {}).get("value") == "$env{QT_ROOT}",
            "CMake presets should obtain the Qt prefix from QT_ROOT")
    require(base_cache.get("Qt6_DIR", {}).get("value") ==
            "$env{QT_ROOT}/lib/cmake/Qt6",
            "CMake presets should bind Qt6 discovery to QT_ROOT")
    require(base_cache.get("BUILD_TESTING") is True,
            "CMake presets should enable the project test suite")

    require(configure_presets.get("debug", {}).get("inherits") == "base" and
            configure_presets["debug"].get("binaryDir") == "${sourceDir}/build" and
            configure_presets["debug"].get("cacheVariables", {}).get("CMAKE_BUILD_TYPE") == "Debug",
            "debug configure preset should preserve the existing Debug build directory")
    require(configure_presets.get("release", {}).get("inherits") == "base" and
            configure_presets["release"].get("binaryDir") == "${sourceDir}/build-release" and
            configure_presets["release"].get("cacheVariables", {}).get("CMAKE_BUILD_TYPE") == "Release",
            "release configure preset should preserve the existing Release build directory")

    for preset_kind in ("buildPresets", "testPresets"):
        named_presets = {
            preset.get("name"): preset
            for preset in presets.get(preset_kind, [])
        }
        for name, configuration in (("debug", "Debug"), ("release", "Release")):
            require(named_presets.get(name, {}).get("configurePreset") == name and
                    named_presets[name].get("configuration") == configuration,
                    f"{preset_kind} should connect {name} to its configure preset")

    for test_preset in presets.get("testPresets", []):
        require(test_preset.get("output", {}).get("outputOnFailure") is True,
                "CTest presets should print failing test output")

    require("find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS" in root_cmake,
            "top-level CMake should require the approved exact Qt version")
    require("qt_standard_project_setup(REQUIRES 6.8)" in root_cmake,
            "top-level CMake should preserve the Qt 6.8 policy baseline")

    vcpkg_manifest_path = ROOT / "vcpkg.json"
    require(vcpkg_manifest_path.exists(),
            "vcpkg.json should declare reproducible manifest dependencies")
    vcpkg_manifest = json.loads(vcpkg_manifest_path.read_text(encoding="utf-8"))
    require(vcpkg_manifest.get("builtin-baseline") == expected_vcpkg_baseline,
            "vcpkg manifest should pin the approved builtin baseline")

    dependencies_by_name = {
        dependency if isinstance(dependency, str) else dependency.get("name"): dependency
        for dependency in vcpkg_manifest.get("dependencies", [])
    }
    require(set(dependencies_by_name) == {"spdlog", "ffmpeg", "pkgconf"},
            "vcpkg manifest should keep the approved direct dependency set")
    require(dependencies_by_name["spdlog"].get("features") == ["fmt"],
            "spdlog should keep the fmt feature")
    require(dependencies_by_name["ffmpeg"].get("features") == [
                "avcodec", "avfilter", "avformat", "swresample", "swscale"
            ],
            "ffmpeg should keep the approved feature set")

    overrides_by_name = {
        override.get("name"): override.get("version")
        for override in vcpkg_manifest.get("overrides", [])
    }
    require(overrides_by_name == expected_vcpkg_overrides,
            "vcpkg manifest should pin the approved exact direct dependency versions")

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
    plugin_discovery_h = read("core/PluginDiscovery.h")
    plugin_discovery_cpp = read("core/PluginDiscovery.cpp")
    plugin_metadata_validator_h = read("core/PluginMetadataValidator.h")
    plugin_interface_h = read("plugin/IAppPlugin.h")
    require("namespace PluginBased::Plugins::PluginDiscovery" in plugin_discovery_h,
            "stateless plugin discovery logic should live in PluginDiscovery")
    require("namespace PluginBased::Plugins" in plugin_metadata_validator_h,
            "plugin metadata validation types should live in the PluginBased::Plugins namespace")
    require("plugins.json" in plugin_discovery_cpp and "QJsonDocument" in plugin_discovery_cpp,
            "PluginDiscovery should read plugins.json at runtime")
    require("manifestFilePath" in plugin_discovery_cpp,
            "PluginDiscovery should resolve the manifest path from the plugin directory")
    require("PluginManager: plugin manifest not found" in plugin_discovery_cpp,
            "PluginDiscovery should not fall back to loading all plugins when manifest is missing")
    require("PluginDiscovery::manifestPluginNames" in plugin_manager_cpp,
            "PluginManager should load only plugin names listed by the manifest through PluginDiscovery")
    require("PluginBasedPluginApiVersion" in plugin_interface_h,
            "IAppPlugin should expose a stable plugin API version constant")
    require("PluginBasedPluginAbiVersion" in plugin_interface_h,
            "IAppPlugin should expose a stable plugin ABI version constant")
    require("class PluginMetadataValidator" in plugin_metadata_validator_h,
            "plugin metadata schema validation should live in PluginMetadataValidator")
    require("PluginMetadataValidator::validateFile" in plugin_manager_cpp,
            "PluginManager should validate plugin metadata before loading libraries")
    require("metadataJsonPathForLibrary" in plugin_manager_cpp,
            "PluginManager should resolve sidecar plugin metadata JSON paths")

    for plugin_json in ("plugins/DummyPlugin/DummyPlugin.json", "plugins/PlayPlugin/PlayPlugin.json"):
        metadata_root = json.loads((ROOT / plugin_json).read_text(encoding="utf-8"))
        metadata = metadata_root.get("MetaData", {})
        require(metadata_root.get("IID") == "com.pluginbased.IAppPlugin/1.0",
                f"{plugin_json} should use the current IAppPlugin IID")
        for key in ("schemaVersion", "apiVersion", "abiVersion", "id", "name", "version", "description", "hasQml"):
            require(key in metadata, f"{plugin_json} should define MetaData.{key}")

    app_config_h = read("app/AppConfig.h")
    app_config_cpp = read("app/AppConfig.cpp")
    require("namespace PluginBased::App" in app_config_h,
            "AppConfig should live in the PluginBased::App namespace")
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
    require("namespace PluginBased::App" in app_controller_h,
            "AppController should live in the PluginBased::App namespace")
    require("QML_NAMED_ELEMENT(AppController)" in app_controller_h,
            "AppController should keep the public QML type name stable")
    require("currentTheme READ currentTheme" in app_controller_h,
            "AppController should expose currentTheme to QML")
    require("setTheme(" in app_controller_h,
            "AppController should expose setTheme to QML")
    require("toggleTheme" in app_controller_h,
            "AppController should expose toggleTheme to QML")
    require("class AppThemeService" in app_theme_service_h,
            "Theme orchestration should live in AppThemeService")
    require("namespace PluginBased::App" in app_theme_service_h,
            "AppThemeService should live in the PluginBased::App namespace")
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
    vcpkg_checkout_start = workflow.index("- name: Checkout vcpkg")
    install_qt_start = workflow.index("- name: Install Qt", vcpkg_checkout_start)
    vcpkg_checkout = workflow[vcpkg_checkout_start:install_qt_start]
    require(f"ref: {expected_vcpkg_baseline}" in vcpkg_checkout,
            "CI should checkout the same vcpkg commit used as builtin-baseline")
    require("fetch-depth: 0" in vcpkg_checkout,
            "CI should fetch full vcpkg history so manifest overrides can resolve port trees")
    require("ctest --test-dir build" in workflow,
            "CI should run CTest")
    require("cmake --build build --parallel" in workflow,
            "CI should build the project")
    require("QtQuickComponents" in workflow,
            "CI should checkout or provide QtQuickComponents")
    require("CMAKE_TOOLCHAIN_FILE" in workflow,
            "CI should configure with vcpkg manifest dependencies")

    main_cpp = read("app/main.cpp")
    require('"/qml"' in main_cpp,
            "main.cpp should add appDir/qml as a runtime QML import path")
    require("../Resources/qml" in main_cpp,
            "main.cpp should add macOS bundle Resources/qml as a runtime QML import path")
    require("QML_IMPORT_PATH" in main_cpp,
            "main.cpp should keep QML_IMPORT_PATH as a development fallback")
    require("addRuntimeQmlImportPaths" in main_cpp,
            "main.cpp should centralize runtime QML import path setup")
    require(main_cpp.find("addRuntimeQmlImportPaths") < main_cpp.find("engine.load(entryUrl)"),
            "main.cpp should add QML import paths before loading the root QML entry")


if __name__ == "__main__":
    main()
