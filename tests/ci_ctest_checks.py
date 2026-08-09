#!/usr/bin/env python3

import json
import os
import platform
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def write_fake_dependencies(vcpkg_root, qt_root):
    (vcpkg_root / "scripts" / "buildsystems").mkdir(parents=True)
    (vcpkg_root / "scripts" / "buildsystems" / "vcpkg.cmake").write_text(
        "# test toolchain\n", encoding="utf-8"
    )
    vcpkg_executable = "vcpkg.exe" if platform.system() == "Windows" else "vcpkg"
    (vcpkg_root / vcpkg_executable).write_text("test tool\n", encoding="utf-8")
    (qt_root / "lib" / "cmake" / "Qt6").mkdir(parents=True)
    (qt_root / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").write_text(
        "# test Qt config\n", encoding="utf-8"
    )
    for package in ("Qt6Multimedia", "Qt6ShaderTools"):
        package_dir = qt_root / "lib" / "cmake" / package
        package_dir.mkdir(parents=True)
        (package_dir / f"{package}Config.cmake").write_text(
            "# test Qt module config\n", encoding="utf-8"
        )


def write_toolchain_test_project(source_dir, presets):
    source_dir.mkdir(exist_ok=True)
    (source_dir / "cmake").mkdir()
    shutil.copy2(
        ROOT / "cmake" / "PluginBasedToolchain.cmake",
        source_dir / "cmake" / "PluginBasedToolchain.cmake",
    )
    isolated_presets = json.loads(json.dumps(presets))
    debug_preset = next(
        preset
        for preset in isolated_presets["configurePresets"]
        if preset.get("name") == "debug"
    )
    debug_preset["binaryDir"] = "${sourceDir}/build"
    isolated_presets["buildPresets"] = []
    isolated_presets["testPresets"] = []
    (source_dir / "CMakePresets.json").write_text(
        json.dumps(isolated_presets), encoding="utf-8"
    )
    (source_dir / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        "project(ToolchainResolutionCheck NONE)\n"
        "file(WRITE \"${CMAKE_BINARY_DIR}/resolved-paths.txt\" "
        "\"${PLUGINBASED_VCPKG_ROOT}\\n${Qt6_DIR}\\n\")\n",
        encoding="utf-8",
    )


def verify_preset_respects_external_environment(presets):
    with tempfile.TemporaryDirectory() as temporary_directory:
        source_dir = Path(temporary_directory) / "source"
        fake_home = Path(temporary_directory) / "home"
        fake_home.mkdir()
        vcpkg_root = fake_home / "portable-vcpkg"
        qt_root = fake_home / "portable-qt"
        write_fake_dependencies(vcpkg_root, qt_root)
        write_toolchain_test_project(source_dir, presets)

        environment = os.environ.copy()
        environment["HOME"] = str(fake_home)
        environment["VCPKG_ROOT"] = str(vcpkg_root)
        environment["QT_ROOT"] = str(qt_root)
        result = subprocess.run(
            ["cmake", "--preset", "debug"],
            cwd=source_dir,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )
        require(result.returncode == 0,
                "debug preset should configure with externally supplied dependency roots:\n"
                + result.stdout + result.stderr)

        resolved_paths = (source_dir / "build" / "resolved-paths.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        require(resolved_paths == [str(vcpkg_root), str(qt_root / "lib/cmake/Qt6")],
                "project toolchain should preserve externally supplied dependency roots")


def verify_build_environment_setup_tool():
    setup_tool = ROOT / "tools" / "setup_build_environment.py"
    require(setup_tool.exists(),
            "the build environment setup tool should be available")

    with tempfile.TemporaryDirectory() as temporary_directory:
        temporary_root = Path(temporary_directory)
        project_root = temporary_root / "project"
        fake_home = temporary_root / "home"
        vcpkg_root = fake_home / "vcpkg" / "vcpkg-master"
        qt_kit = {
            "Darwin": "macos",
            "Linux": "gcc_64",
            "Windows": "msvc2022_64",
        }[platform.system()]
        qt_root = fake_home / "Qt" / "6.8.3" / qt_kit
        downloads_root = vcpkg_root / "downloads"
        project_root.mkdir()
        write_fake_dependencies(vcpkg_root, qt_root)
        downloads_root.mkdir(parents=True)
        write_toolchain_test_project(project_root, json.loads(read("CMakePresets.json")))

        environment = os.environ.copy()
        environment["HOME"] = str(fake_home)
        environment.pop("VCPKG_ROOT", None)
        environment.pop("QT_ROOT", None)
        environment.pop("VCPKG_DOWNLOADS", None)
        result = subprocess.run(
            [
                "python3", str(setup_tool),
                "--project-root", str(project_root),
                "--no-install",
                "--configure", "debug",
            ],
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )
        require(result.returncode == 0,
                "setup tool should accept valid existing dependencies:\n"
                + result.stdout + result.stderr)

        require(not (project_root / "CMakeUserPresets.json").exists(),
                "setup tool should not generate machine-specific presets")
        resolved_paths = (project_root / "build" / "resolved-paths.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        require(resolved_paths == [
                    str(vcpkg_root.resolve()),
                    str((qt_root / "lib" / "cmake" / "Qt6").resolve()),
                ],
                "setup tool should configure with the resolved default dependency roots")


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
    verify_preset_respects_external_environment(presets)
    verify_build_environment_setup_tool()
    require(presets.get("version") == 3,
            "CMake presets should use schema version 3 for CMake 3.24")
    require(presets.get("cmakeMinimumRequired") == {
                "major": 3, "minor": 24, "patch": 0
            },
            "CMake presets should match the project CMake 3.24 floor")
    configure_presets = {
        preset.get("name"): preset
        for preset in presets.get("configurePresets", [])
    }
    base_preset = configure_presets.get("base", {})
    require(base_preset.get("hidden") is True,
            "CMake presets should keep shared configuration in a hidden base preset")
    require(base_preset.get("generator") == "Ninja",
            "CMake presets should use the Ninja generator")
    require("environment" not in base_preset,
            "shared presets should not override machine- or CI-specific dependency roots")
    require(base_preset.get("toolchainFile") ==
            "${sourceDir}/cmake/PluginBasedToolchain.cmake",
            "CMake presets should use the repository-owned dependency resolver")
    base_cache = base_preset.get("cacheVariables", {})
    require("CMAKE_PREFIX_PATH" not in base_cache and "Qt6_DIR" not in base_cache,
            "dependency paths should be resolved by the project toolchain")
    require(base_cache.get("BUILD_TESTING") is True,
            "CMake presets should enable the project test suite")
    require(base_cache.get("CMAKE_EXPORT_COMPILE_COMMANDS") is True,
            "CMake presets should generate compile_commands.json for editor tooling")

    vscode_settings_path = ROOT / ".vscode" / "settings.json"
    vscode_tasks_path = ROOT / ".vscode" / "tasks.json"
    vscode_launch_path = ROOT / ".vscode" / "launch.json"
    require(vscode_settings_path.exists() and vscode_tasks_path.exists() and
            vscode_launch_path.exists(),
            "fresh clones should include shared VS Code CMake and QML configuration")
    vscode_settings = vscode_settings_path.read_text(encoding="utf-8")
    vscode_tasks = vscode_tasks_path.read_text(encoding="utf-8")
    vscode_launch = vscode_launch_path.read_text(encoding="utf-8")
    require('"cmake.useCMakePresets": "always"' in vscode_settings,
            "VS Code should configure through repository CMake presets")
    require('"qt-qml.qmlls.enabled": true' in vscode_settings and
            '"--build-dir=${workspaceFolder}/build"' in vscode_settings,
            "VS Code should provide QML language-server build metadata")
    require("customExePath" not in vscode_settings and
            '"cmake.environment"' not in vscode_settings,
            "VS Code settings should not hard-code machine dependency paths")
    require('"--preset", "debug"' in vscode_tasks and
            '"--preset", "release"' in vscode_tasks,
            "VS Code tasks should expose the shared debug and release presets")
    require("local-debug" not in vscode_tasks and "local-release" not in vscode_tasks and
            "VCPKG_ROOT" not in vscode_tasks and "QT_ROOT" not in vscode_tasks,
            "VS Code tasks should not carry local preset names or dependency paths")
    require('"program": "${command:cmake.launchTargetPath}"' in vscode_launch and
            "PluginBasedApp.app" not in vscode_launch,
            "VS Code launch configuration should use the active CMake target on every platform")

    require(configure_presets.get("debug", {}).get("inherits") == "base" and
            configure_presets["debug"].get("binaryDir") == "${sourceDir}/build" and
            configure_presets["debug"].get("cacheVariables", {}).get("CMAKE_BUILD_TYPE") == "Debug",
            "debug configure preset should preserve the existing Debug build directory")
    require(configure_presets.get("release", {}).get("inherits") == "base" and
            configure_presets["release"].get("binaryDir") == "${sourceDir}/build-release" and
            configure_presets["release"].get("cacheVariables", {}).get("CMAKE_BUILD_TYPE") == "Release",
            "release configure preset should preserve the existing Release build directory")
    require(set(configure_presets) == {"base", "debug", "release"},
            "repository configure presets should expose only debug and release")

    for preset_kind in ("buildPresets", "testPresets"):
        named_presets = {
            preset.get("name"): preset
            for preset in presets.get(preset_kind, [])
        }
        require(set(named_presets) == {"debug", "release"},
                f"{preset_kind} should expose only debug and release")
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
    require("GIT_REPOSITORY https://github.com/Zacus/QtQuickComponents.git" in root_cmake,
            "QtQuickComponents should be fetched from its canonical repository")
    require("GIT_TAG 8e376dfc50e703a49b4e66aa1302e5fcd6df2cde" in root_cmake,
            "QtQuickComponents should be pinned to the approved commit")
    require('SOURCE_DIR "${CMAKE_SOURCE_DIR}/../QtQuickComponents"' not in root_cmake,
            "fresh clones should not require an adjacent QtQuickComponents checkout")

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
    qt_find_package_start = root_cmake.index("find_package(Qt6")
    qt_find_package_end = root_cmake.index(")", qt_find_package_start)
    qt_find_package = root_cmake[qt_find_package_start:qt_find_package_end]
    require("GuiPrivate" not in qt_find_package,
            "Qt6::GuiPrivate comes from the Gui package and must not be requested as a standalone Qt component")
    play_plugin_cmake = read("plugins/PlayPlugin/CMakeLists.txt")
    require("Qt6::GuiPrivate" in play_plugin_cmake,
            "PlayPlugin should keep linking the Gui private target supplied by the Qt Gui package")
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
    require("tags:" in workflow and "- 'v*'" in workflow,
            "CI should build version tags")
    require("permissions:\n  contents: read" in workflow,
            "workflow permissions should default to read-only contents")
    require("concurrency:" in workflow and
            "!startsWith(github.ref, 'refs/tags/')" in workflow,
            "CI should cancel stale branch runs but preserve tag releases")
    require("jobs:\n  build_and_package:" in workflow,
            "CI should use one cross-platform build and package job")

    build_job_start = workflow.index("  build_and_package:")
    release_job_start = workflow.index("\n  release:", build_job_start)
    build_job = workflow[build_job_start:release_job_start]
    release_job = workflow[release_job_start:]

    require("fail-fast: false" in build_job and
            "timeout-minutes: 180" in build_job,
            "matrix jobs should be independent and bounded")
    require(len(re.findall(r"^\s+- platform:", build_job, re.MULTILINE)) == 3,
            "CI matrix should define exactly three native platforms")
    for required in (
        "runner: macos-14", "qt_host: mac", "qt_arch: clang_64",
        "qt_kit: macos", "package_glob: '*.dmg'",
        "runner: ubuntu-22.04", "qt_host: linux", "qt_arch: gcc_64",
        "qt_kit: gcc_64", "package_glob: '*.tar.gz'",
        "runner: windows-2022", "qt_host: windows",
        "qt_arch: win64_msvc2022_64", "qt_kit: msvc2022_64",
        "package_glob: '*.zip'",
    ):
        require(required in build_job, f"CI matrix missing {required}")

    require("github.event_name == 'pull_request' && 'debug' || 'release'" in build_job,
            "pull requests should use Debug and package events should use Release")
    require("github.event_name == 'pull_request' && 'build' || 'build-release'" in build_job,
            "CI diagnostic paths should follow the selected preset")
    require("ilammy/msvc-dev-cmd@v1" in build_job,
            "Windows should initialize the VS 2022 compiler environment")
    require("patchelf" in build_job,
            "Linux should install its required packaging tool")

    vcpkg_checkout_start = workflow.index("- name: Checkout vcpkg")
    install_qt_start = workflow.index("- name: Install Qt", vcpkg_checkout_start)
    vcpkg_checkout = workflow[vcpkg_checkout_start:install_qt_start]
    require(f"ref: {expected_vcpkg_baseline}" in vcpkg_checkout,
            "CI should checkout the same vcpkg commit used as builtin-baseline")
    require("fetch-depth: 0" in vcpkg_checkout,
            "CI should fetch full vcpkg history so manifest overrides can resolve port trees")
    require("Checkout QtQuickComponents" not in workflow,
            "CI should rely on the same pinned FetchContent declaration as local builds")
    require("VCPKG_ROOT: ${{ github.workspace }}/vcpkg" in workflow,
            "CI should expose the fixed vcpkg checkout to CMake Presets")
    require("QT_ROOT: ${{ runner.temp }}/Qt/6.8.3/${{ matrix.qt_kit }}" in workflow,
            "CI should expose each exact official Qt kit to CMake Presets")
    require("VCPKG_DOWNLOADS: ${{ runner.temp }}/vcpkg-downloads" in workflow,
            "CI should use an explicit reusable vcpkg downloads directory")
    require("path: ${{ runner.temp }}/vcpkg-downloads" in workflow,
            "CI should cache the configured vcpkg downloads directory")
    require("path: ${{ runner.temp }}/vcpkg-binary-cache" in workflow,
            "CI should cache compiled vcpkg archives in an explicit directory")
    require(expected_vcpkg_baseline in workflow and
            "matrix.qt_arch" in workflow and
            "hashFiles('PluginBased/vcpkg.json')" in workflow,
            "vcpkg cache keys should include baseline, architecture, and manifest")
    require("ninja --version" in workflow,
            "CI should verify the Ninja generator is available")
    require("actions/setup-python@v5" in workflow and "python-version: '3.12'" in workflow,
            "CI should provide the pinned Python line used by aqtinstall")
    require("aqtinstall==3.3.0" in workflow,
            "CI should pin the Qt installer version")
    require('aqt install-qt --outputdir "${RUNNER_TEMP}/Qt"' in workflow and
            '"${{ matrix.qt_host }}" desktop 6.8.3 "${{ matrix.qt_arch }}"' in workflow and
            "-m qtmultimedia qtshadertools" in workflow,
            "CI should install only the required official Qt 6.8.3 matrix kits")
    require("brew install qt" not in workflow and "brew reinstall qt" not in workflow,
            "CI should not fall back to Homebrew Qt")
    for package_file in (
        "lib/cmake/Qt6/Qt6Config.cmake",
        "lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake",
        "lib/cmake/Qt6ShaderTools/Qt6ShaderToolsConfig.cmake",
    ):
        require(package_file in workflow,
                f"CI should validate restored Qt package {package_file}")
    require('cmake --preset "${BUILD_PRESET}" --fresh' in build_job,
            "CI should fresh-configure through the event-selected preset")
    require('cmake --build --preset "${BUILD_PRESET}" --parallel' in build_job,
            "CI should build through the event-selected preset")
    require('ctest --preset "${BUILD_PRESET}"' in build_job,
            "CI should test the event-selected build")

    for required in (
        "${GITHUB_REF_NAME#v}", "./package.sh --version",
        "-DPLUGINBASED_OFFICIAL_BUILD=ON",
        "-DPLUGINBASED_EXPECTED_TAG=${GITHUB_REF_NAME}",
    ):
        require(required in build_job, f"tag validation missing {required}")
    require("${RUNNER_TEMP}/configure-${{ matrix.platform }}.log" in build_job and
            "${RUNNER_TEMP}/ctest-${{ matrix.platform }}.log" in build_job,
            "CI logs should stay outside the source tree for clean official builds")
    require("./package.sh --skip-build" in workflow,
            "CI should exercise the production packaging entry point")
    require(build_job.count("if: github.event_name != 'pull_request'") >= 3,
            "pull requests should not package, checksum, or upload release artifacts")
    require("hashlib.sha256" in build_job and
            "len(packages) != 1" in build_job,
            "CI should require and checksum exactly one native package")
    require("startsWith(github.ref, 'refs/tags/v') && 365 || 30" in build_job,
            "tag artifacts should retain 365 days and ordinary artifacts 30 days")
    require("PluginBased-${{ matrix.platform }}-package" in build_job,
            "package artifact names should be matrix-specific")
    require("if: failure()" in build_job and
            "PluginBased-${{ matrix.platform }}-diagnostics" in build_job and
            "if-no-files-found: ignore" in build_job and
            "retention-days: 30" in build_job,
            "failure diagnostics should be matrix-specific and retained 30 days")

    require("if: startsWith(github.ref, 'refs/tags/v')" in release_job and
            "needs: build_and_package" in release_job,
            "release publication should wait for every tagged matrix job")
    require("permissions:\n      contents: write" in release_job and
            "contents: write" not in build_job,
            "only the publication job should write repository contents")
    require("actions/download-artifact@v4" in release_job and
            "pattern: PluginBased-*-package" in release_job and
            "merge-multiple: true" in release_job,
            "release publication should reuse all matrix artifacts")
    require("hashlib.sha256" in release_job and
            "gh release view" in release_job and
            "gh release create" in release_job and
            "--verify-tag" in release_job,
            "release publication should revalidate checksums and create a verified tag release")
    require("--clobber" not in release_job and "--prerelease" not in release_job,
            "release publication should not overwrite or infer prerelease status")

    main_cpp = read("app/main.cpp")
    app_controller_h = read("app/AppController.h")
    require('setApplicationVersion("1.0.0")' not in main_cpp and
            'QStringLiteral("1.0.0")' not in app_controller_h,
            "runtime application version should come from BuildInfo")
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
