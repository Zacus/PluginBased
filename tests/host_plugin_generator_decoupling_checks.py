#!/usr/bin/env python3

"""Architecture guard for host plugin path and plugin generator boundaries."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    path_resolver_h_path = ROOT / "app/PluginPathResolver.h"
    path_resolver_cpp_path = ROOT / "app/PluginPathResolver.cpp"
    options_h_path = ROOT / "tools/plugin_generator/PluginGeneratorOptions.h"
    renderer_h_path = ROOT / "tools/plugin_generator/PluginTemplateRenderer.h"
    renderer_cpp_path = ROOT / "tools/plugin_generator/PluginTemplateRenderer.cpp"
    writer_h_path = ROOT / "tools/plugin_generator/PluginScaffoldWriter.h"
    writer_cpp_path = ROOT / "tools/plugin_generator/PluginScaffoldWriter.cpp"

    require(path_resolver_h_path.exists(), "PluginPathResolver.h should exist")
    require(path_resolver_cpp_path.exists(), "PluginPathResolver.cpp should exist")
    require(options_h_path.exists(), "PluginGeneratorOptions.h should exist")
    require(renderer_h_path.exists(), "PluginTemplateRenderer.h should exist")
    require(renderer_cpp_path.exists(), "PluginTemplateRenderer.cpp should exist")
    require(writer_h_path.exists(), "PluginScaffoldWriter.h should exist")
    require(writer_cpp_path.exists(), "PluginScaffoldWriter.cpp should exist")

    app_controller_cpp = read("app/AppController.cpp")
    app_cmake = read("app/CMakeLists.txt")
    path_resolver_h = read("app/PluginPathResolver.h")
    path_resolver_cpp = read("app/PluginPathResolver.cpp")
    generator_h = read("tools/plugin_generator/PluginTemplateGenerator.h")
    generator_cpp = read("tools/plugin_generator/PluginTemplateGenerator.cpp")
    generator_cmake = read("tools/plugin_generator/CMakeLists.txt")
    renderer_h = read("tools/plugin_generator/PluginTemplateRenderer.h")
    renderer_cpp = read("tools/plugin_generator/PluginTemplateRenderer.cpp")
    writer_h = read("tools/plugin_generator/PluginScaffoldWriter.h")
    writer_cpp = read("tools/plugin_generator/PluginScaffoldWriter.cpp")
    root_cmake = read("CMakeLists.txt")

    require("Resolves the runtime plugin directory for AppController" in path_resolver_h,
            "PluginPathResolver.h should explain its file purpose")
    require("Implements host plugin directory discovery for development and packaged layouts" in path_resolver_cpp,
            "PluginPathResolver.cpp should explain its file purpose")
    require("PluginPathResolver::resolve" in app_controller_cpp,
            "AppController should delegate plugin path discovery")
    require("const QStringList candidates = {" not in app_controller_cpp,
            "AppController should not own plugin path candidate construction")
    require("PluginPathResolver.h" in app_cmake and "PluginPathResolver.cpp" in app_cmake,
            "PluginBasedApp should compile PluginPathResolver")

    require("struct PluginGeneratorOptions" in read("tools/plugin_generator/PluginGeneratorOptions.h"),
            "PluginGeneratorOptions should expose the parsed generator value type")
    require("Renders generated plugin file text without touching the filesystem" in renderer_h,
            "PluginTemplateRenderer.h should explain its file purpose")
    require("Implements text templates used by PluginTemplateGenerator" in renderer_cpp,
            "PluginTemplateRenderer.cpp should explain its file purpose")
    for method in (
        "headerText",
        "sourceText",
        "metadataText",
        "cmakeText",
        "qmlText",
        "translationText",
    ):
        require(method in renderer_h and method in renderer_cpp,
                f"PluginTemplateRenderer should own {method}")
        require(method not in generator_h,
                f"PluginTemplateGenerator should not expose {method}")

    require("Writes rendered plugin scaffold files to disk" in writer_h,
            "PluginScaffoldWriter.h should explain its file purpose")
    require("Implements filesystem writes for generated plugin scaffolds" in writer_cpp,
            "PluginScaffoldWriter.cpp should explain its file purpose")
    require("writePlugin" in writer_h and "copyIconAsset" in writer_cpp,
            "PluginScaffoldWriter should own scaffold writes and icon copying")
    require("writeTextFile" in writer_cpp and "mkpath" in writer_cpp,
            "PluginScaffoldWriter should create directories and write files")
    require("PluginTemplateRenderer" in generator_cpp and "PluginScaffoldWriter" in generator_cpp,
            "PluginTemplateGenerator should delegate rendering and filesystem writing")

    for required in (
        "PluginGeneratorOptions.h",
        "PluginTemplateRenderer.h",
        "PluginTemplateRenderer.cpp",
        "PluginScaffoldWriter.h",
        "PluginScaffoldWriter.cpp",
    ):
        require(required in generator_cmake,
                f"Plugin generator CMake should compile {required}")
    require("host_plugin_generator_decoupling_checks" in root_cmake,
            "CTest should run host/plugin generator decoupling checks")


if __name__ == "__main__":
    main()
