#!/usr/bin/env python3

import importlib.util
import subprocess
import tempfile
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
DEPLOY_PATH = ROOT / "tools" / "deploy.py"
VERIFY_PATH = ROOT / "tools" / "verify.py"


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


def write(path: Path, contents: str = "fixture") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(contents, encoding="utf-8")


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

        layout = {
            "plugins": "Contents/PlugIns",
            "qml": "Contents/Resources/qml",
            "frameworks": "Contents/Frameworks",
        }
        copied = deploy.copy_runtime_resources(build, bundle, layout, "macos")

        assert copied == 5
        assert (bundle / "Contents" / "plugins.json").is_file()
        assert (bundle / "Contents" / "PlugIns" / "DummyPlugin.json").is_file()
        assert (bundle / "Contents" / "PlugIns" / "PlayPlugin.json").is_file()
        assert (bundle / "Contents" / "Resources" / "themes" / "dark.json").is_file()
        assert (bundle / "Contents" / "Resources" / "themes" / "light.json").is_file()


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

        assert paths.root == qt_root
        assert paths.plugins == qt_plugins
        assert paths.qml == qt_qml
        assert paths.libraries == qt_libs

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


def main() -> None:
    deploy = load_deploy_module()
    verifier = load_verify_module()
    test_runtime_resources_are_self_contained(deploy)
    test_qt_query_paths_and_plugin_category_are_preserved(deploy)
    test_existing_framework_rpath_is_not_added_twice(deploy)
    test_runtime_resource_verifier_rejects_incomplete_bundle(verifier)
    test_old_macos_deployment_is_cleaned_before_repackaging(deploy)
    test_macos_packaging_uses_isolated_staging_bundle(deploy)
    test_macdeployqt_scans_build_modules_not_deployment_output(deploy)
    test_missing_qml_framework_dependencies_are_copied(deploy)


if __name__ == "__main__":
    main()
