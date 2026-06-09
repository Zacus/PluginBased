#!/usr/bin/env python3

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(command):
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    require(
        result.returncode == 0,
        f"command failed: {' '.join(command)}\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}",
    )
    return result


def find_smoke_test_binary():
    candidates = [
        BUILD_DIR / "tools" / "plugin_generator" / "PluginGeneratorBackendSmokeTest",
        BUILD_DIR / "tools" / "plugin_generator" / "Debug" / "PluginGeneratorBackendSmokeTest",
        BUILD_DIR / "tools" / "plugin_generator" / "Release" / "PluginGeneratorBackendSmokeTest",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise AssertionError("PluginGeneratorBackendSmokeTest binary was not produced")


def test_backend_smoke_test_passes():
    run(["cmake", "--build", "build", "--target", "PluginGeneratorBackendSmokeTest", "--parallel"])
    smoke_test = find_smoke_test_binary()
    run([str(smoke_test)])


def test_top_level_cmake_uses_plugin_manifest():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    manifest_path = ROOT / "plugins.json"
    require("add_subdirectory(tools/plugin_generator)" in cmake,
            "top-level CMake should build the visual plugin generator")
    require(manifest_path.exists(),
            "top-level CMake should be driven by root plugins.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("plugins") == ["DummyPlugin", "PlayPlugin"],
            "default plugin manifest should list only supported project plugins")
    require("file(GLOB PLUGIN_CMAKELISTS" not in cmake,
            "top-level CMake should not auto-discover every plugin directory")
    require("PLUGIN_MANIFEST" in cmake and "string(JSON" in cmake,
            "top-level CMake should parse the plugin manifest")
    require("add_subdirectory(\"${_plugin_dir}\")" in cmake,
            "top-level CMake should add plugin directories from the manifest")
    require("plugins/DummyPlugin" not in cmake and "plugins/PlayPlugin" not in cmake,
            "top-level CMake should not require manual plugin entries")


def test_visual_qml_exposes_plugin_type_choice():
    qml = ROOT / "tools" / "plugin_generator" / "qml" / "Main.qml"
    require(qml.exists(), "visual generator QML should exist")
    text = qml.read_text(encoding="utf-8")
    require("带 QML 页面" in text and "No-QML 后台插件" in text,
            "QML UI should expose QML and No-QML plugin type choices")
    require("PluginTemplateGenerator" in text,
            "QML UI should call the C++ generator backend")
    require("FileDialog" in text and "选择图片" in text,
            "QML UI should let users choose an image file for the card icon")


def test_generated_qml_uses_component_theme():
    generator_cpp = ROOT / "tools" / "plugin_generator" / "PluginTemplateGenerator.cpp"
    text = generator_cpp.read_text(encoding="utf-8")
    require("import QuickUI.Components 1.0" in text,
            "generated QML plugins should import QuickUI.Components")
    require("ComponentTheme.surface" in text,
            "generated QML plugins should use ComponentTheme surface tokens")
    require("ComponentTheme.textPrimary" in text and "ComponentTheme.textSecondary" in text,
            "generated QML plugins should use ComponentTheme text tokens")


def test_generated_metadata_uses_plugin_schema():
    generator_cpp = ROOT / "tools" / "plugin_generator" / "PluginTemplateGenerator.cpp"
    text = generator_cpp.read_text(encoding="utf-8")
    require('"schemaVersion": 1' in text,
            "generated plugins should declare metadata schemaVersion")
    require('"apiVersion": 1' in text,
            "generated plugins should declare plugin API version")
    require('"abiVersion": 1' in text,
            "generated plugins should declare plugin ABI version")
    require('"id": "%2"' in text,
            "generated plugins should declare stable kebab-case plugin id")
    require('"hasQml": %3' in text,
            "generated plugins should declare whether they provide QML UI")


def test_host_supports_image_card_icons():
    interface = (ROOT / "plugin" / "IAppPlugin.h").read_text(encoding="utf-8")
    manager_h = (ROOT / "core" / "PluginManager.h").read_text(encoding="utf-8")
    manager_cpp = (ROOT / "core" / "PluginManager.cpp").read_text(encoding="utf-8")
    home_panel = (ROOT / "app" / "qml" / "HomePanel.qml").read_text(encoding="utf-8")

    require("virtual QUrl cardIconUrl() const" in interface,
            "IAppPlugin should expose an optional image icon URL")
    require("pluginCardIconUrl" in manager_h and "pluginCardIconUrl" in manager_cpp,
            "PluginManager should expose plugin image icon URLs to QML")
    require("property url iconUrl" in home_panel and "Image {" in home_panel,
            "HomePanel PluginCard should render image icons")
    require("iconUrl.toString().length === 0" in home_panel,
            "HomePanel should keep text icon fallback when no image URL is provided")


def main():
    test_backend_smoke_test_passes()
    test_top_level_cmake_uses_plugin_manifest()
    test_visual_qml_exposes_plugin_type_choice()
    test_generated_qml_uses_component_theme()
    test_generated_metadata_uses_plugin_schema()
    test_host_supports_image_card_icons()


if __name__ == "__main__":
    main()
