#!/usr/bin/env python3

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

    require("include(CTest)" in root_cmake,
            "top-level CMake should include CTest")
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
