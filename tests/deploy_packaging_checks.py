#!/usr/bin/env python3

import os
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
DEPLOY_PATH = ROOT / "tools" / "deploy.py"
VERIFY_PATH = ROOT / "tools" / "verify.py"
SETUP_PATH = ROOT / "tools" / "setup_build_environment.py"
PACKAGE_SCRIPT = ROOT / "package.sh"

BUILD_INFO = {
    "schemaVersion": 1,
    "productName": "PluginBased",
    "productVersion": "1.0.0",
    "displayVersion": "1.0.0+g12345678",
    "gitCommit": "1234567890abcdef1234567890abcdef12345678",
    "gitShortCommit": "12345678",
    "gitTag": "",
    "gitTreeState": "clean",
    "buildType": "Release",
    "platform": "Linux",
    "architecture": "x86_64",
    "compiler": "GNU 13.2.0",
    "qtVersion": "6.8.3",
}


def load_deploy_module():
    spec = importlib.util.spec_from_file_location("pluginbased_deploy", DEPLOY_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {DEPLOY_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_verify_module():
    spec = importlib.util.spec_from_file_location("pluginbased_verify", VERIFY_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {VERIFY_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_setup_module():
    spec = importlib.util.spec_from_file_location("pluginbased_setup", SETUP_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {SETUP_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write(path: Path, contents: str = "fixture") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


def make_package_fixture(root: Path) -> Path:
    script = root / "package.sh"
    shutil.copy2(PACKAGE_SCRIPT, script)
    write(root / "tools" / "package.json", "{}\n")
    return script


def package_environment(fake_bin=None):
    environment = os.environ.copy()
    if fake_bin:
        environment["PATH"] = f"{fake_bin}{os.pathsep}{environment['PATH']}"
    environment.pop("QT_DIR", None)
    environment.pop("QT_ROOT", None)
    return environment


def make_fake_cmake(root: Path):
    fake_bin = root / "fake-bin"
    log = root / "cmake.log"
    fake = fake_bin / "cmake"
    write(fake, """#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "$*" >> "${PACKAGE_TEST_CMAKE_LOG}"
if [[ "${1:-}" == "-E" && "${2:-}" == "remove_directory" ]]; then
    rm -rf -- "${3}"
    exit 0
fi
if [[ "${1:-}" == "--preset" ]]; then
    mkdir -p build-release
    {
        printf 'CMAKE_BUILD_TYPE:STRING=Release\\n'
        printf 'CMAKE_GENERATOR:INTERNAL=Ninja\\n'
        printf 'CMAKE_TOOLCHAIN_FILE:FILEPATH=%s/cmake/PluginBasedToolchain.cmake\\n' "$PWD"
        printf 'PLUGINBASED_QT_ROOT:PATH=%s/Qt/6.8.3/macos\\n' "$PWD"
    } > build-release/CMakeCache.txt
fi
""")
    fake.chmod(0o755)
    return fake_bin, log


def test_package_script_uses_release_preset_by_default() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        fake_bin, log = make_fake_cmake(root)
        environment = package_environment(fake_bin)
        environment["PACKAGE_TEST_CMAKE_LOG"] = str(log)
        write(root / "build-release" / "_deps" / "stale-subbuild" / "CMakeCache.txt")

        result = subprocess.run(
            [str(script), "--build-only"], cwd=root,
            text=True, capture_output=True, env=environment)

        assert result.returncode == 0, result.stderr
        calls = log.read_text(encoding="utf-8").splitlines()
        assert calls[0] == f"-E remove_directory {root / 'build-release'}"
        assert calls[1] == "--preset release --fresh"
        assert calls[2].startswith("--build --preset release --parallel ")
        assert not (root / "build-release" / "_deps" / "stale-subbuild").exists()


def test_deploy_help_has_no_third_party_python_dependency() -> None:
    result = subprocess.run(
        [sys.executable, "-S", str(DEPLOY_PATH), "--help"],
        text=True, capture_output=True)

    assert result.returncode == 0, result.stderr
    assert "PluginBased" in result.stdout


def test_package_script_rejects_stale_generator_when_skipping_build() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        write(root / "build-release" / "CMakeCache.txt", "\n".join([
            "CMAKE_BUILD_TYPE:STRING=Release",
            "CMAKE_GENERATOR:INTERNAL=Unix Makefiles",
            f"CMAKE_TOOLCHAIN_FILE:FILEPATH={root}/cmake/PluginBasedToolchain.cmake",
            "",
        ]))

        result = subprocess.run(
            [str(script), "--skip-build"], cwd=root,
            text=True, capture_output=True, env=package_environment())

        assert result.returncode != 0
        assert "Ninja" in result.stderr


def test_package_script_rejects_debug_cache_when_skipping_build() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        write(root / "build-release" / "CMakeCache.txt",
              "CMAKE_BUILD_TYPE:STRING=Debug\n")

        result = subprocess.run(
            [str(script), "--skip-build"], cwd=root,
            text=True, capture_output=True, env=package_environment())

        assert result.returncode != 0
        assert "Release" in result.stderr


def test_package_script_keeps_custom_multiconfig_build_support() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        custom = root / "custom-build"
        write(custom / "CMakeCache.txt",
              "CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n")
        fake_bin, log = make_fake_cmake(root)
        environment = package_environment(fake_bin)
        environment["PACKAGE_TEST_CMAKE_LOG"] = str(log)

        result = subprocess.run(
            [str(script), "--build-only", str(custom)], cwd=root,
            text=True, capture_output=True, env=environment)

        assert result.returncode == 0, result.stderr
        call = log.read_text(encoding="utf-8").strip()
        assert call.startswith(f"--build {custom} --config Release --parallel ")


def test_package_script_uses_cached_qt_root_for_deployment() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        write(root / "build-release" / "CMakeCache.txt", "\n".join([
            "CMAKE_BUILD_TYPE:STRING=Release",
            "CMAKE_GENERATOR:INTERNAL=Ninja",
            f"CMAKE_TOOLCHAIN_FILE:FILEPATH={root}/cmake/PluginBasedToolchain.cmake",
            "PLUGINBASED_QT_ROOT:PATH=/opt/Qt/6.8.3/macos",
            "",
        ]))
        write(root / "tools" / "deploy.py",
              "import sys\nprint('\\n'.join(sys.argv[1:]))\n")
        result = subprocess.run(
            [str(script), "--skip-build", "--no-verify"], cwd=root,
            text=True, capture_output=True, env=package_environment())

        assert result.returncode == 0, result.stderr
        assert "--qt-dir\n/opt/Qt/6.8.3/macos" in result.stdout
        assert "--skip-verify" in result.stdout


def test_package_script_normalizes_windows_style_cached_paths() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        fake_bin = root / "fake-bin"
        cygpath = fake_bin / "cygpath"
        write(cygpath, """#!/usr/bin/env bash
value="${!#}"
printf '%s\\n' "$value" | tr '\\\\' '/'
""")
        cygpath.chmod(0o755)

        toolchain = root / "cmake" / "PluginBasedToolchain.cmake"
        qt_root = root / "Qt" / "6.8.3" / "msvc2022_64"
        windows_toolchain = str(toolchain).replace("/", "\\")
        windows_qt_root = str(qt_root).replace("/", "\\")
        write(root / "build-release" / "CMakeCache.txt", "\n".join([
            "CMAKE_BUILD_TYPE:STRING=Release",
            "CMAKE_GENERATOR:INTERNAL=Ninja",
            f"CMAKE_TOOLCHAIN_FILE:FILEPATH={windows_toolchain}",
            f"PLUGINBASED_QT_ROOT:PATH={windows_qt_root}",
            "",
        ]))
        write(root / "tools" / "deploy.py",
              "import sys\nprint('\\n'.join(sys.argv[1:]))\n")
        environment = package_environment(fake_bin)
        environment["QT_ROOT"] = windows_qt_root

        result = subprocess.run(
            [str(script), "--skip-build", "--no-verify"], cwd=root,
            text=True, capture_output=True, env=environment)

        assert result.returncode == 0, result.stderr
        assert f"--qt-dir\n{qt_root}" in result.stdout


def test_qt_install_paths_use_build_qt_instead_of_path(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        build = root / "build"
        official_qt = root / "official-qt"
        path_qt = root / "path-qt"
        write(build / "CMakeCache.txt",
              f"PLUGINBASED_QT_ROOT:PATH={official_qt}\n")

        for qt_root in (official_qt, path_qt):
            qtpaths = qt_root / "bin" / "qtpaths"
            write(qtpaths, "#!/usr/bin/env bash\n" + "\n".join([
                f"echo 'QT_INSTALL_PREFIX:{qt_root}'",
                f"echo 'QT_INSTALL_PLUGINS:{qt_root}/plugins'",
                f"echo 'QT_INSTALL_QML:{qt_root}/qml'",
                f"echo 'QT_INSTALL_LIBS:{qt_root}/lib'",
                "",
            ]))
            qtpaths.chmod(0o755)

        with mock.patch.dict(os.environ, {
                "PATH": f"{path_qt / 'bin'}{os.pathsep}{os.environ['PATH']}"}):
            paths = deploy.detect_qt_install_paths(build)

        assert paths.root == official_qt.resolve()
        assert paths.prefix == official_qt.resolve()
        assert paths.plugins == official_qt.resolve() / "plugins"


def test_qt_override_must_match_build_qt(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        build = root / "build"
        write(build / "CMakeCache.txt",
              f"PLUGINBASED_QT_ROOT:PATH={root / 'build-qt'}\n")

        try:
            deploy.resolve_qt_root(build, root / "other-qt")
        except ValueError as error:
            assert "does not match" in str(error)
        else:
            raise AssertionError("a deployment Qt override must match the build Qt")


def test_runtime_resources_are_self_contained(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        build = root / "build"
        bundle = root / "PluginBasedApp.app"
        write(build / "plugins.json", '{"plugins":["DummyPlugin","PlayPlugin"]}')
        write(build / "plugins" / "DummyPlugin.json")
        write(build / "plugins" / "PlayPlugin.json")
        write(build / "themes" / "dark.json")
        write(build / "themes" / "light.json")
        write(build / "build-info.json", json.dumps(BUILD_INFO))

        layout = {
            "plugins": "Contents/PlugIns",
            "qml": "Contents/Resources/qml",
            "frameworks": "Contents/Frameworks",
        }
        copied = deploy.copy_runtime_resources(build, bundle, layout, "macos")

        assert copied == 6
        assert (bundle / "Contents" / "plugins.json").is_file()
        assert (bundle / "Contents" / "PlugIns" / "DummyPlugin.json").is_file()
        assert (bundle / "Contents" / "PlugIns" / "PlayPlugin.json").is_file()
        assert (bundle / "Contents" / "Resources" / "themes" / "dark.json").is_file()
        assert (bundle / "Contents" / "Resources" / "themes" / "light.json").is_file()
        assert (bundle / "Contents" / "Resources" / "build-info.json").is_file()

        for platform_name, platform_layout in (
            ("linux", {"plugins": "bin/plugins"}),
            ("windows", {"plugins": "plugins"}),
        ):
            stage = root / platform_name
            copied = deploy.copy_runtime_resources(
                build, stage, platform_layout, platform_name)
            assert copied == 6
            assert (stage / "build-info.json").is_file()


def test_build_info_verifier_enforces_schema_privacy_and_version(verifier) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary) / "stage"
        path = stage / "build-info.json"

        write(path, json.dumps(BUILD_INFO))
        assert verifier.scan_build_info(stage, expected_version="1.0.0") == []

        path.unlink()
        assert any("缺失构建信息" in issue
                   for issue in verifier.scan_build_info(stage))

        write(path, "{")
        assert any("无效构建信息" in issue
                   for issue in verifier.scan_build_info(stage))

        document = dict(BUILD_INFO)
        document["schemaVersion"] = 2
        write(path, json.dumps(document))
        assert any("schemaVersion" in issue
                   for issue in verifier.scan_build_info(stage))

        document = dict(BUILD_INFO)
        document["productVersion"] = "2.0.0"
        write(path, json.dumps(document))
        assert any("2.0.0" in issue and "1.0.0" in issue
                   for issue in verifier.scan_build_info(
                       stage, expected_version="1.0.0"))

        document = dict(BUILD_INFO)
        document["gitCommit"] = "not-a-commit"
        write(path, json.dumps(document))
        assert any("gitCommit" in issue
                   for issue in verifier.scan_build_info(stage))

        for forbidden_key in ("gitRef", "sourceDir"):
            document = dict(BUILD_INFO)
            document[forbidden_key] = "private-context"
            write(path, json.dumps(document))
            assert any(forbidden_key in issue
                       for issue in verifier.scan_build_info(stage))


def test_qt_query_paths_and_plugin_category_are_preserved(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        qt_root = root / "qt-link"
        qt_plugins = root / "qt-install" / "plugins"
        qt_qml = root / "qt-install" / "qml"
        qt_libs = root / "qt-install" / "lib"
        stage = root / "stage"
        query_output = "\n".join([
            f"QT_INSTALL_PREFIX:{root / 'qt-install'}",
            f"QT_INSTALL_PLUGINS:{qt_plugins}",
            f"QT_INSTALL_QML:{qt_qml}",
            f"QT_INSTALL_LIBS:{qt_libs}",
        ])
        write(qt_plugins / "platforms" / "libqcocoa.dylib")
        write(qt_qml / "Qt" / "labs" / "folderlistmodel" / "qmldir")
        write(qt_libs / "QtDBus.framework" / "QtDBus")

        completed = subprocess.CompletedProcess(
            ["qtpaths", "--query"], 0, stdout=query_output, stderr="")
        with mock.patch.object(deploy, "find_qt_tool", return_value=qt_root / "bin" / "qtpaths"), \
             mock.patch.object(deploy, "detect_qt_dir", return_value=qt_root), \
             mock.patch.object(deploy, "run", return_value=completed):
            paths = deploy.detect_qt_install_paths(root / "build")

        assert paths.root == qt_root.resolve()
        assert paths.plugins == qt_plugins.resolve()
        assert paths.qml == qt_qml.resolve()
        assert paths.libraries == qt_libs.resolve()

        layout = {
            "qt_plugins": "Contents/PlugIns",
            "qml": "Contents/Resources/qml",
            "frameworks": "Contents/Frameworks",
        }
        copied = deploy.copy_qt_runtime_plugins(
            paths,
            stage,
            layout,
            [
                "plugins/platforms/libqcocoa.dylib",
                "qml/Qt/labs/folderlistmodel",
                "frameworks/QtDBus.framework",
            ],
            "macos",
        )

        assert copied == 3
        assert (stage / "Contents" / "PlugIns" / "platforms" / "libqcocoa.dylib").is_file()
        assert (stage / "Contents" / "Resources" / "qml" / "Qt" / "labs" /
                "folderlistmodel" / "qmldir").is_file()
        assert (stage / "Contents" / "Frameworks" / "QtDBus.framework" /
                "QtDBus").is_file()


def test_existing_framework_rpath_is_not_added_twice(deploy) -> None:
    otool_output = """
Load command 1
          cmd LC_RPATH
      cmdsize 48
         path @executable_path/../Frameworks (offset 12)
"""
    calls = []

    def fake_run(command, **kwargs):
        calls.append(command)
        return subprocess.CompletedProcess(command, 0, stdout=otool_output, stderr="")

    with mock.patch.object(deploy, "run", side_effect=fake_run):
        deploy.fix_rpath_macos(Path("/tmp/PluginBasedApp"))

    assert not any(command[:2] == ["install_name_tool", "-add_rpath"]
                   for command in calls)


def test_runtime_resource_verifier_rejects_incomplete_bundle(verifier) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        bundle = Path(temporary) / "PluginBasedApp.app"
        write(bundle / "Contents" / "plugins.json",
              '{"plugins":["DummyPlugin","PlayPlugin"]}')
        write(bundle / "Contents" / "PlugIns" / "DummyPlugin.json")
        write(bundle / "Contents" / "PlugIns" / "PlayPlugin.json")
        write(bundle / "Contents" / "PlugIns" / "libDummyPlugin.dylib")
        write(bundle / "Contents" / "PlugIns" / "libPlayPlugin.dylib")
        write(bundle / "Contents" / "Resources" / "themes" / "dark.json")

        assert verifier.scan_runtime_resources(bundle) == []

        (bundle / "Contents" / "PlugIns" / "PlayPlugin.json").unlink()
        issues = verifier.scan_runtime_resources(bundle)
        assert any("PlayPlugin.json" in issue for issue in issues)


def test_verifier_rejects_existing_host_absolute_dependency(verifier) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        stage = root / "stage"
        binary = stage / "libPlugin.dylib"
        host_dependency = root / "host" / "libQtLeak.dylib"
        write(binary)
        write(host_dependency)

        with mock.patch.object(
                verifier, "get_direct_deps", return_value=[str(host_dependency)]):
            issues, checked = verifier.scan_bundle(stage)

        assert checked == 1
        assert any(str(host_dependency) in issue for issue in issues)


def test_universal_macho_architecture_headers_are_not_dependencies(deploy, verifier) -> None:
    binary = "/tmp/libUniversal.dylib"
    output = f"""{binary} (architecture x86_64):
\t@rpath/libUniversal.dylib (compatibility version 1.0.0, current version 1.0.0)
\t/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (compatibility version 45.0.0, current version 2575.0.0)
{binary} (architecture arm64):
\t@rpath/libUniversal.dylib (compatibility version 1.0.0, current version 1.0.0)
\t/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (compatibility version 45.0.0, current version 2575.0.0)
"""
    expected = [
        "@rpath/libUniversal.dylib",
        "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit",
        "@rpath/libUniversal.dylib",
        "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit",
    ]

    assert deploy.parse_otool_dependencies(output) == expected
    assert verifier.parse_otool_dependencies(output) == expected


def test_linux_verifier_accepts_bundle_resolution_and_rejects_host_resolution(verifier) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        stage = root / "stage"
        binary = stage / "bin" / "PluginBasedApp"
        bundled = stage / "lib" / "libQt6Core.so.6"
        host = root / "host" / "libavcodec.so.61"
        for path in (binary, bundled, host):
            write(path)
        binary.chmod(0o755)

        with mock.patch.object(verifier.platform, "system", return_value="Linux"), \
             mock.patch.object(
                 verifier,
                 "get_direct_deps",
                 side_effect=lambda path: (
                     [str(bundled), str(host), "libMissing.so"]
                     if path == binary else []
                 ),
             ):
            issues, checked = verifier.scan_bundle(stage)

        assert checked == 1
        assert not any(str(bundled) in issue for issue in issues)
        assert any(str(host) in issue for issue in issues)
        assert any("libMissing.so" in issue for issue in issues)


def test_windows_verifier_resolves_packaged_dll_names(verifier) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary) / "stage"
        executable = stage / "PluginBasedApp.exe"
        packaged = stage / "Qt6Core.dll"
        write(executable)
        write(packaged)

        with mock.patch.object(verifier.platform, "system", return_value="Windows"), \
             mock.patch.object(
                 verifier,
                 "get_direct_deps",
                 side_effect=lambda path: (
                     ["Qt6Core.dll", "KERNEL32.dll", "Missing.dll"]
                     if path == executable else []
                 ),
             ):
            issues, checked = verifier.scan_bundle(stage)

        assert checked == 1
        assert not any("Qt6Core.dll" in issue for issue in issues)
        assert not any("KERNEL32.dll" in issue for issue in issues)
        assert any("Missing.dll" in issue for issue in issues)


def test_partial_qt_install_is_not_ready(setup) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        qt_root = Path(temporary) / "Qt" / "6.8.3" / "macos"
        write(qt_root / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake")

        assert not setup.qt_is_ready(qt_root)

        write(qt_root / "lib" / "cmake" / "Qt6Multimedia" /
              "Qt6MultimediaConfig.cmake")
        write(qt_root / "lib" / "cmake" / "Qt6ShaderTools" /
              "Qt6ShaderToolsConfig.cmake")
        assert setup.qt_is_ready(qt_root)


def test_old_macos_deployment_is_cleaned_before_repackaging(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        bundle = Path(temporary) / "PluginBasedApp.app"
        write(bundle / "Contents" / "Frameworks" / "libStale.dylib")
        write(bundle / "Contents" / "PlugIns" / "stale.plugin")
        write(bundle / "Contents" / "Resources" / "qml" / "stale.qml")
        write(bundle / "Contents" / "Resources" / "qt.conf")
        write(bundle / "Contents" / "_CodeSignature" / "CodeResources")
        write(bundle / "Contents" / "Resources" / "themes" / "keep.json")

        deploy.clean_macos_deployment(bundle, {
            "plugins": "Contents/PlugIns",
            "qml": "Contents/Resources/qml",
            "frameworks": "Contents/Frameworks",
        })

        assert not (bundle / "Contents" / "Frameworks").exists()
        assert not (bundle / "Contents" / "PlugIns").exists()
        assert not (bundle / "Contents" / "Resources" / "qml").exists()
        assert not (bundle / "Contents" / "Resources" / "qt.conf").exists()
        assert not (bundle / "Contents" / "_CodeSignature").exists()
        assert (bundle / "Contents" / "Resources" / "themes" / "keep.json").is_file()


def test_macos_packaging_uses_isolated_staging_bundle(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        build = Path(temporary) / "build"
        source_bundle = build / "app" / "PluginBasedApp.app"
        write(source_bundle / "Contents" / "MacOS" / "PluginBasedApp")
        write(source_bundle / "Contents" / "Frameworks" / "libStale.dylib")
        layout = {
            "plugins": "Contents/PlugIns",
            "qml": "Contents/Resources/qml",
            "frameworks": "Contents/Frameworks",
        }

        staged_bundle = deploy.prepare_macos_bundle(
            build, "PluginBasedApp", layout)

        assert staged_bundle == build / "_package_macos" / "PluginBasedApp.app"
        assert (staged_bundle / "Contents" / "MacOS" / "PluginBasedApp").is_file()
        assert not (staged_bundle / "Contents" / "Frameworks").exists()
        assert (source_bundle / "Contents" / "Frameworks" / "libStale.dylib").is_file()


def test_platform_staging_directory_is_persistent_and_clean(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        build = Path(temporary) / "build"
        stale = build / "_package_linux" / "PluginBased-1.0.0-linux-x86_64" / "stale"
        write(stale)

        stage = deploy.prepare_platform_staging(
            build, "linux", "PluginBased-1.0.0-linux-x86_64")

        assert stage == build / "_package_linux" / "PluginBased-1.0.0-linux-x86_64"
        assert stage.is_dir()
        assert not stale.exists()


def test_macdeployqt_scans_build_modules_not_deployment_output(deploy) -> None:
    build = Path("/tmp/build")
    bundle = build / "_package_macos" / "PluginBasedApp.app"
    command = deploy.macdeployqt_command(
        Path("/opt/qt/bin/macdeployqt"),
        bundle,
        build,
        [Path("/src/app/qml"), build / "QuickUI"],
    )

    assert f"-qmlimport={build}" in command
    assert "-always-overwrite" in command
    assert "-no-plugins" in command
    assert "-verbose=0" in command
    assert f"-qmldir={bundle / 'Contents' / 'Resources' / 'qml'}" not in command
    assert "-qmldir=/src/app/qml" in command
    assert f"-qmldir={build / 'QuickUI'}" in command


def test_missing_qml_framework_dependencies_are_copied(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        bundle = root / "PluginBasedApp.app"
        executable = bundle / "Contents" / "MacOS" / "PluginBasedApp"
        qt_libraries = root / "qt" / "lib"
        third_party = root / "third-party" / "libExtra.1.dylib"
        write(executable)
        write(qt_libraries / "QtExtra.framework" / "Versions" / "A" / "QtExtra")
        write(third_party)

        def fake_run(command, **kwargs):
            if command[:2] == ["otool", "-L"] and Path(command[2]) == executable:
                output = (
                    f"{executable}:\n"
                    "\t@rpath/QtExtra.framework/Versions/A/QtExtra "
                    "(compatibility version 6.0.0, current version 6.9.0)\n"
                    f"\t{third_party} "
                    "(compatibility version 1.0.0, current version 1.0.0)\n"
                )
            else:
                output = f"{command[-1]}:\n"
            return subprocess.CompletedProcess(command, 0, stdout=output, stderr="")

        with mock.patch.object(deploy, "run", side_effect=fake_run):
            copied = deploy.copy_missing_macos_runtime_dependencies(
                bundle, qt_libraries)

        assert copied == 2
        assert (bundle / "Contents" / "Frameworks" / "QtExtra.framework" /
                "Versions" / "A" / "QtExtra").is_file()
        assert (bundle / "Contents" / "Frameworks" / "libExtra.1.dylib").is_file()


def test_runtime_dependency_closure_includes_plugin_transitive_dependencies(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        stage = root / "stage"
        main_binary = stage / "bin" / "PluginBasedApp"
        plugin_binary = stage / "bin" / "plugins" / "libPlayPlugin.so"
        dependency_root = root / "dependencies"
        logger = dependency_root / "libPluginBasedLogger.so"
        ffmpeg = dependency_root / "libavcodec.so"
        destination = stage / "lib"
        for path in (main_binary, plugin_binary, logger, ffmpeg):
            write(path)

        graph = {
            "PluginBasedApp": [],
            "libPlayPlugin.so": ["libPluginBasedLogger.so"],
            "libPluginBasedLogger.so": ["libavcodec.so"],
            "libavcodec.so": [],
        }

        copied = deploy.copy_runtime_dependency_closure(
            [main_binary, plugin_binary],
            destination,
            lambda binary: graph[binary.name],
            [dependency_root],
            lambda dependency: False,
        )

        assert copied == [
            destination / "libPluginBasedLogger.so",
            destination / "libavcodec.so",
        ]
        assert all(path.is_file() for path in copied)


def test_runtime_dependency_closure_rejects_unresolved_dependency(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        binary = root / "stage" / "PluginBasedApp.exe"
        write(binary)

        try:
            deploy.copy_runtime_dependency_closure(
                [binary],
                root / "stage",
                lambda _: ["missing-runtime.dll"],
                [],
                lambda dependency: False,
            )
        except RuntimeError as error:
            assert "missing-runtime.dll" in str(error)
        else:
            raise AssertionError("unresolved runtime dependencies must fail packaging")


def test_runtime_dependency_closure_preserves_soname_symlink_name(deploy) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        binary = root / "stage" / "PluginBasedApp"
        destination = root / "stage" / "lib"
        real_library = root / "qt" / "libQt6Core.so.6.8.3"
        soname_link = root / "qt" / "libQt6Core.so.6"
        write(binary)
        write(real_library)
        soname_link.symlink_to(real_library.name)

        copied = deploy.copy_runtime_dependency_closure(
            [binary],
            destination,
            lambda path: [str(soname_link)] if path == binary.resolve() else [],
            [root / "qt"],
            lambda dependency: False,
        )

        assert copied == [destination / "libQt6Core.so.6"]
        assert (destination / "libQt6Core.so.6").is_file()


def test_linux_dependency_parser_preserves_unresolved_names(deploy) -> None:
    dependencies = deploy.parse_ldd_dependencies("""
        linux-vdso.so.1 (0x00007fff)
        libQt6Core.so.6 => /opt/Qt/lib/libQt6Core.so.6 (0x00007f00)
        libavcodec.so.61 => not found
        /lib64/ld-linux-x86-64.so.2 (0x00007f01)
    """)

    assert dependencies == [
        "/opt/Qt/lib/libQt6Core.so.6",
        "libavcodec.so.61",
        "/lib64/ld-linux-x86-64.so.2",
    ]


def test_windows_dependency_parser_extracts_dll_names(deploy) -> None:
    dependencies = deploy.parse_dumpbin_dependencies("""
        Image has the following dependencies:

            Qt6Core.dll
            avcodec-61.dll
            KERNEL32.dll

        Summary
    """)

    assert dependencies == ["Qt6Core.dll", "avcodec-61.dll", "KERNEL32.dll"]


def test_staging_verification_is_an_explicit_packaging_gate(deploy) -> None:
    stage = Path("/tmp/package-stage")
    build = Path("/tmp/package-build")
    with mock.patch.object(deploy, "run") as run_mock:
        with mock.patch.object(deploy, "_read_version", return_value="1.0.0"):
            deploy.verify_staging(stage, build, enabled=True)

    command = run_mock.call_args.args[0]
    assert command == [
        sys.executable,
        str(Path(deploy.__file__).parent / "verify.py"),
        "--stage-dir",
        str(stage),
        "--expected-version",
        "1.0.0",
    ]
    assert run_mock.call_args.kwargs["check"] is True

    with mock.patch.object(deploy, "run") as run_mock:
        deploy.verify_staging(stage, build, enabled=False)
    run_mock.assert_not_called()


def main() -> None:
    deploy = load_deploy_module()
    verifier = load_verify_module()
    setup = load_setup_module()
    test_deploy_help_has_no_third_party_python_dependency()
    test_package_script_uses_release_preset_by_default()
    test_package_script_rejects_stale_generator_when_skipping_build()
    test_package_script_rejects_debug_cache_when_skipping_build()
    test_package_script_keeps_custom_multiconfig_build_support()
    test_package_script_uses_cached_qt_root_for_deployment()
    test_package_script_normalizes_windows_style_cached_paths()
    test_qt_install_paths_use_build_qt_instead_of_path(deploy)
    test_qt_override_must_match_build_qt(deploy)
    test_runtime_resources_are_self_contained(deploy)
    test_build_info_verifier_enforces_schema_privacy_and_version(verifier)
    test_qt_query_paths_and_plugin_category_are_preserved(deploy)
    test_existing_framework_rpath_is_not_added_twice(deploy)
    test_runtime_resource_verifier_rejects_incomplete_bundle(verifier)
    test_verifier_rejects_existing_host_absolute_dependency(verifier)
    test_universal_macho_architecture_headers_are_not_dependencies(deploy, verifier)
    test_linux_verifier_accepts_bundle_resolution_and_rejects_host_resolution(verifier)
    test_windows_verifier_resolves_packaged_dll_names(verifier)
    test_partial_qt_install_is_not_ready(setup)
    test_old_macos_deployment_is_cleaned_before_repackaging(deploy)
    test_macos_packaging_uses_isolated_staging_bundle(deploy)
    test_platform_staging_directory_is_persistent_and_clean(deploy)
    test_macdeployqt_scans_build_modules_not_deployment_output(deploy)
    test_missing_qml_framework_dependencies_are_copied(deploy)
    test_runtime_dependency_closure_includes_plugin_transitive_dependencies(deploy)
    test_runtime_dependency_closure_rejects_unresolved_dependency(deploy)
    test_runtime_dependency_closure_preserves_soname_symlink_name(deploy)
    test_linux_dependency_parser_preserves_unresolved_names(deploy)
    test_windows_dependency_parser_extracts_dll_names(deploy)
    test_staging_verification_is_an_explicit_packaging_gate(deploy)


if __name__ == "__main__":
    main()
