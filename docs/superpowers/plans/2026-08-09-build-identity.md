# Build Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every PluginBased binary and package one stable product version plus a privacy-safe, Git-backed build identity that users can report and developers can reproduce.

**Architecture:** Keep `project(PluginBased VERSION 1.0.0)` as the only manually maintained product version. A repository-owned CMake generator creates a private header and `build-info.json`; an immutable C++ `BuildInfo` component exposes the same data to the executable, logs, CLI, and QML, while packaging copies and validates the generated JSON instead of recreating metadata.

**Tech Stack:** CMake 3.24, C++17, Qt 6.8.3 Core/Gui/Quick/Test, QML, Python 3 standard library tests, Git, existing Python deployment tools.

## Global Constraints

- `project(PluginBased VERSION 1.0.0)` remains the only manually maintained product-version source.
- Do not add a checked-in `VERSION`, `version.xml`, or `build-info.json` source file.
- Generated files are `${CMAKE_BINARY_DIR}/generated/BuildInfoData.h` and `${CMAKE_BINARY_DIR}/build-info.json`; neither is committed.
- Persist full/short commit, exact tag, and `clean`/`dirty`/`unknown`; never persist branch names, CI refs, usernames, hostnames, absolute paths, or Git remotes.
- Use a fixed eight-character short commit for the user-visible build number.
- Ordinary UI shows only product version and short build number; full details stay in diagnostic output.
- Local builds tolerate dirty or missing Git metadata; official builds reject unknown/dirty sources and mismatched tags.
- The application consumes compiled constants and must not require external JSON at runtime.
- Keep plugin versions and plugin API/ABI contracts unchanged.
- Preserve existing user changes in the dirty worktree; stage and commit only files named by each task.

---

## File Map

- `cmake/GenerateBuildInfo.cmake`: query Git, validate official-build rules, derive display version, and atomically generate C++/JSON data.
- `cmake/PluginBasedBuildInfo.cmake`: connect the generator to a CMake target and refresh metadata before builds.
- `app/BuildInfo.h`, `app/BuildInfo.cpp`: immutable Qt value API over generated constants.
- `app/qml/AboutDialog.qml`: concise user-facing version dialog and diagnostic-copy action.
- `app/version.rc.in`: Windows VERSIONINFO generated from `PROJECT_VERSION`.
- `tests/build_info_generation_checks.py`: real temporary-repository tests for generator behavior.
- `tests/tst_build_info.cpp`: C++ contract tests for getters, JSON, privacy, and summaries.
- `tests/build_info_cli_checks.py`: executable-level tests for `--version` and `--build-info`.
- `tests/native_version_metadata_checks.py`: built macOS bundle version assertions.
- `tools/deploy.py`, `tools/verify.py`: copy and validate build identity in every package layout.
- `tests/deploy_packaging_checks.py`: package-copy and rejection tests.
- `CMakeLists.txt`, `app/CMakeLists.txt`: register generation, targets, and tests.
- `app/main.cpp`, `app/AppController.h`, `app/AppController.cpp`, `app/qml/main.qml`, `translations/pluginbased_zh_CN.ts`: runtime, logging, CLI, and About integration.
- `README.md`, `BUILD.md`: developer and support contracts.

---

### Task 1: Deterministic Build-Information Generator

**Files:**
- Create: `cmake/GenerateBuildInfo.cmake`
- Create: `cmake/PluginBasedBuildInfo.cmake`
- Create: `tests/build_info_generation_checks.py`
- Modify: `CMakeLists.txt` in the `BUILD_TESTING` test-registration block

**Interfaces:**
- Consumes: `PROJECT_NAME`, `PROJECT_VERSION`, compiler/system/Qt values supplied with `-D`, source and output paths, `PLUGINBASED_OFFICIAL_BUILD`, and `PLUGINBASED_EXPECTED_TAG`.
- Produces: `${CMAKE_BINARY_DIR}/generated/BuildInfoData.h`, `${CMAKE_BINARY_DIR}/build-info.json`, and `pluginbased_attach_build_info(<target>)`.

- [ ] **Step 1: Write generator behavior tests against a real temporary Git repository**

Create `tests/build_info_generation_checks.py` with helpers that invoke `cmake -P` directly and assertions based on literal expected values:

```python
#!/usr/bin/env python3
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "cmake" / "GenerateBuildInfo.cmake"


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=True)


def initialize_repository(source):
    run(["git", "init", "-q"], source)
    run(["git", "config", "user.email", "build-info@example.invalid"], source)
    run(["git", "config", "user.name", "Build Info Test"], source)
    (source / "tracked.txt").write_text("clean\n", encoding="utf-8")
    run(["git", "add", "tracked.txt"], source)
    run(["git", "commit", "-qm", "fixture"], source)
    return run(["git", "rev-parse", "HEAD"], source).stdout.strip()


def generate(source, output, *, official=False, expected_tag=""):
    return subprocess.run([
        "cmake",
        f"-DPLUGINBASED_SOURCE_DIR={source}",
        f"-DPLUGINBASED_OUTPUT_HEADER={output / 'generated/BuildInfoData.h'}",
        f"-DPLUGINBASED_OUTPUT_JSON={output / 'build-info.json'}",
        "-DPLUGINBASED_PRODUCT_NAME=PluginBased",
        "-DPLUGINBASED_PRODUCT_VERSION=1.0.0",
        "-DPLUGINBASED_BUILD_TYPE=Release",
        "-DPLUGINBASED_PLATFORM=Linux",
        "-DPLUGINBASED_ARCHITECTURE=x86_64",
        "-DPLUGINBASED_COMPILER=GNU 13.2.0",
        "-DPLUGINBASED_QT_VERSION=6.8.3",
        f"-DPLUGINBASED_OFFICIAL_BUILD={'ON' if official else 'OFF'}",
        f"-DPLUGINBASED_EXPECTED_TAG={expected_tag}",
        "-P", str(GENERATOR),
    ], text=True, capture_output=True)
```

Add independent tests proving:

```python
def test_clean_repository_records_exact_commit_and_no_branch():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        commit = initialize_repository(source)
        result = generate(source, output)
        assert result.returncode == 0, result.stderr
        document = json.loads((output / "build-info.json").read_text())
        assert document["productVersion"] == "1.0.0"
        assert document["gitCommit"] == commit
        assert document["gitShortCommit"] == commit[:8]
        assert document["gitTreeState"] == "clean"
        assert document["displayVersion"] == f"1.0.0+g{commit[:8]}"
        assert "gitRef" not in document and "sourceDir" not in document


def test_dirty_and_missing_git_have_explicit_identities():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        commit = initialize_repository(source)
        (source / "tracked.txt").write_text("dirty\n", encoding="utf-8")
        result = generate(source, output)
        assert result.returncode == 0, result.stderr
        dirty = json.loads((output / "build-info.json").read_text())
        assert dirty["gitTreeState"] == "dirty"
        assert dirty["displayVersion"] == f"1.0.0+g{commit[:8]}.dirty"

        exported, exported_output = root / "exported", root / "exported-out"
        exported.mkdir()
        result = generate(exported, exported_output)
        assert result.returncode == 0, result.stderr
        unknown = json.loads((exported_output / "build-info.json").read_text())
        assert unknown["gitCommit"] == ""
        assert unknown["gitShortCommit"] == "unknown"
        assert unknown["gitTreeState"] == "unknown"
        assert unknown["displayVersion"] == "1.0.0+unknown"


def test_matching_official_tag_is_accepted_and_mismatch_is_rejected():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)
        run(["git", "tag", "v1.0.0"], source)
        accepted = generate(source, output, official=True, expected_tag="v1.0.0")
        assert accepted.returncode == 0, accepted.stderr
        document = json.loads((output / "build-info.json").read_text())
        assert document["gitTag"] == "v1.0.0"
        assert document["displayVersion"] == "1.0.0"

        rejected = generate(source, output, official=True, expected_tag="v2.0.0")
        assert rejected.returncode != 0
        assert "v2.0.0" in rejected.stderr and "1.0.0" in rejected.stderr


def test_generation_does_not_rewrite_unchanged_outputs():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source, output = root / "source", root / "out"
        source.mkdir()
        initialize_repository(source)
        first = generate(source, output)
        assert first.returncode == 0, first.stderr
        paths = [output / "generated/BuildInfoData.h", output / "build-info.json"]
        before = [(path.stat().st_ino, path.stat().st_mtime_ns) for path in paths]
        second = generate(source, output)
        assert second.returncode == 0, second.stderr
        after = [(path.stat().st_ino, path.stat().st_mtime_ns) for path in paths]
        assert after == before
```

Add `main()` that calls every `test_*` function and prints `build info generation checks passed`; do not mock Git or CMake.

- [ ] **Step 2: Run the generator tests and verify the expected RED state**

Run:

```bash
python3 tests/build_info_generation_checks.py
```

Expected: FAIL because `cmake/GenerateBuildInfo.cmake` does not exist and no output JSON is created.

- [ ] **Step 3: Implement Git collection, validation, escaping, and write-if-different generation**

Implement `cmake/GenerateBuildInfo.cmake` with these exact rules:

```cmake
foreach(_required IN ITEMS
        PLUGINBASED_SOURCE_DIR PLUGINBASED_OUTPUT_HEADER PLUGINBASED_OUTPUT_JSON
        PLUGINBASED_PRODUCT_NAME PLUGINBASED_PRODUCT_VERSION
        PLUGINBASED_BUILD_TYPE PLUGINBASED_PLATFORM PLUGINBASED_ARCHITECTURE
        PLUGINBASED_COMPILER PLUGINBASED_QT_VERSION)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Missing required build-info input: ${_required}")
    endif()
endforeach()

set(_git_commit "")
set(_git_short "unknown")
set(_git_tag "")
set(_tree_state "unknown")
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE AND EXISTS "${PLUGINBASED_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${PLUGINBASED_SOURCE_DIR}" rev-parse HEAD
        RESULT_VARIABLE _git_result OUTPUT_VARIABLE _git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(LENGTH "${_git_commit}" _git_commit_length)
    if(_git_result EQUAL 0
            AND _git_commit MATCHES "^[0-9a-fA-F]+$"
            AND (_git_commit_length EQUAL 40 OR _git_commit_length EQUAL 64))
        string(SUBSTRING "${_git_commit}" 0 8 _git_short)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${PLUGINBASED_SOURCE_DIR}"
                    status --porcelain --untracked-files=normal
            OUTPUT_VARIABLE _git_status OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_git_status STREQUAL "")
            set(_tree_state "clean")
        else()
            set(_tree_state "dirty")
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${PLUGINBASED_SOURCE_DIR}"
                    describe --tags --exact-match --match "v[0-9]*" HEAD
            RESULT_VARIABLE _tag_result OUTPUT_VARIABLE _git_tag
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT _tag_result EQUAL 0)
            set(_git_tag "")
        endif()
    endif()
endif()
```

Validate `PLUGINBASED_EXPECTED_TAG` against `^v[0-9]+\.[0-9]+\.[0-9]+$`, enforce `v${PLUGINBASED_PRODUCT_VERSION}`, known commit, clean tree, and matching exact tag only when `PLUGINBASED_OFFICIAL_BUILD` is true. Derive display version using the four approved rules.

Add local functions that escape backslash, quote, newline, carriage return, and tab independently for C++ and JSON. Generate header constants in namespace `PluginBased::BuildInfoData` and JSON schema version 1. Write each candidate to `<output>.tmp`, compare with the existing output via `cmake -E compare_files`, and rename only when different.

Implement `cmake/PluginBasedBuildInfo.cmake`:

```cmake
function(pluginbased_attach_build_info target)
    set(_generated_dir "${CMAKE_BINARY_DIR}/generated")
    set(_header "${_generated_dir}/BuildInfoData.h")
    set(_json "${CMAKE_BINARY_DIR}/build-info.json")
    set(_generator "${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake")

    set(_generator_args
        "-DPLUGINBASED_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DPLUGINBASED_OUTPUT_HEADER=${_header}"
        "-DPLUGINBASED_OUTPUT_JSON=${_json}"
        "-DPLUGINBASED_PRODUCT_NAME=${PROJECT_NAME}"
        "-DPLUGINBASED_PRODUCT_VERSION=${PROJECT_VERSION}"
        "-DPLUGINBASED_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DPLUGINBASED_PLATFORM=${CMAKE_SYSTEM_NAME}"
        "-DPLUGINBASED_ARCHITECTURE=${CMAKE_SYSTEM_PROCESSOR}"
        "-DPLUGINBASED_COMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        "-DPLUGINBASED_QT_VERSION=${Qt6_VERSION}"
        "-DPLUGINBASED_OFFICIAL_BUILD=${PLUGINBASED_OFFICIAL_BUILD}"
        "-DPLUGINBASED_EXPECTED_TAG=${PLUGINBASED_EXPECTED_TAG}")

    execute_process(COMMAND "${CMAKE_COMMAND}" ${_generator_args} -P "${_generator}"
                    COMMAND_ERROR_IS_FATAL ANY)
    add_custom_target(${target}_build_info_refresh ALL
        COMMAND "${CMAKE_COMMAND}" ${_generator_args} -P "${_generator}"
        BYPRODUCTS "${_header}" "${_json}"
        VERBATIM)
    add_dependencies(${target} ${target}_build_info_refresh)
    target_include_directories(${target} PRIVATE "${_generated_dir}")
    set(PLUGINBASED_BUILD_INFO_JSON "${_json}" PARENT_SCOPE)
endfunction()
```

Declare cache inputs at the root:

```cmake
option(PLUGINBASED_OFFICIAL_BUILD "Require clean, matching tagged source" OFF)
set(PLUGINBASED_EXPECTED_TAG "" CACHE STRING "Expected vMAJOR.MINOR.PATCH release tag")
```

Register `build_info_generation_checks` under `BUILD_TESTING`.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run:

```bash
python3 tests/build_info_generation_checks.py
```

Expected: all clean, dirty, missing-Git, official-tag, privacy, malformed-input, and idempotence cases pass.

- [ ] **Step 5: Commit only the generator slice**

```bash
git add cmake/GenerateBuildInfo.cmake cmake/PluginBasedBuildInfo.cmake tests/build_info_generation_checks.py CMakeLists.txt
git commit -m "[功能新增] 生成可追溯构建身份"
```

---

### Task 2: Immutable C++ BuildInfo And Command-Line Identity

**Files:**
- Create: `app/BuildInfo.h`
- Create: `app/BuildInfo.cpp`
- Create: `tests/tst_build_info.cpp`
- Create: `tests/build_info_cli_checks.py`
- Modify: `app/CMakeLists.txt`
- Modify: `app/main.cpp`
- Modify: `CMakeLists.txt` test-registration block

**Interfaces:**
- Consumes: generated `PluginBased::BuildInfoData` constants and `${CMAKE_BINARY_DIR}/build-info.json`.
- Produces: internal `PluginBasedBuildInfo` target and `PluginBased::App::BuildInfo` static value API.

- [ ] **Step 1: Write C++ tests for build-number, privacy, JSON, and summary contracts**

Create `tests/tst_build_info.cpp` using Qt Test:

```cpp
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include "BuildInfo.h"

using PluginBased::App::BuildInfo;

class BuildInfoTest : public QObject
{
    Q_OBJECT
private slots:
    void productVersionIsStable();
    void diagnosticJsonMatchesGetters();
    void diagnosticDataDoesNotExposeMachineOrBranchIdentity();
};

void BuildInfoTest::productVersionIsStable()
{
    QCOMPARE(BuildInfo::productName(), QStringLiteral("PluginBased"));
    QCOMPARE(BuildInfo::productVersion(), QStringLiteral(PLUGINBASED_TEST_VERSION));
    QVERIFY(!BuildInfo::buildNumber().isEmpty());
    QVERIFY(BuildInfo::conciseVersion().startsWith(
        QStringLiteral("PluginBased ") + BuildInfo::productVersion()));
}

void BuildInfoTest::diagnosticJsonMatchesGetters()
{
    const QJsonObject object = QJsonDocument::fromJson(BuildInfo::json()).object();
    QCOMPARE(object.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(object.value(QStringLiteral("productVersion")).toString(),
             BuildInfo::productVersion());
    QCOMPARE(object.value(QStringLiteral("displayVersion")).toString(),
             BuildInfo::displayVersion());
    QCOMPARE(object.value(QStringLiteral("gitCommit")).toString(),
             BuildInfo::gitCommit());
}

void BuildInfoTest::diagnosticDataDoesNotExposeMachineOrBranchIdentity()
{
    const QJsonObject object = QJsonDocument::fromJson(BuildInfo::json()).object();
    QVERIFY(!object.contains(QStringLiteral("gitRef")));
    QVERIFY(!object.contains(QStringLiteral("hostname")));
    QVERIFY(!object.contains(QStringLiteral("sourceDir")));
    QVERIFY(!BuildInfo::diagnosticSummary().contains(QStringLiteral("gitRef")));
}

QTEST_APPLESS_MAIN(BuildInfoTest)
#include "tst_build_info.moc"
```

- [ ] **Step 2: Configure/build the focused test and verify RED**

Run:

```bash
cmake --preset debug --fresh
cmake --build --preset debug --target BuildInfoTest --parallel
```

Expected: build fails because `BuildInfo.h` and `PluginBasedBuildInfo` do not exist.

- [ ] **Step 3: Implement the immutable BuildInfo component and target**

Define this API in `app/BuildInfo.h`:

```cpp
#pragma once

#include <QByteArray>
#include <QString>

namespace PluginBased::App {

class BuildInfo final
{
public:
    BuildInfo() = delete;

    static QString productName();
    static QString productVersion();
    static QString displayVersion();
    static QString buildNumber();
    static QString gitCommit();
    static QString gitTag();
    static QString treeState();
    static QString buildType();
    static QString platform();
    static QString architecture();
    static QString compiler();
    static QString qtVersion();
    static QString conciseVersion();
    static QString diagnosticSummary();
    static QByteArray json();
};

} // namespace PluginBased::App
```

Implement getters from `BuildInfoData.h`. Build `json()` through `QJsonObject` and `QJsonDocument`, using the same schema fields as the generated file. Make `diagnosticSummary()` a stable multiline string without branch, host, path, or remote data.

In `app/CMakeLists.txt`, create and attach the internal library before `PluginBasedApp`:

```cmake
add_library(PluginBasedBuildInfo STATIC BuildInfo.h BuildInfo.cpp)
target_link_libraries(PluginBasedBuildInfo PUBLIC Qt6::Core)
target_include_directories(PluginBasedBuildInfo PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
include("${CMAKE_SOURCE_DIR}/cmake/PluginBasedBuildInfo.cmake")
pluginbased_attach_build_info(PluginBasedBuildInfo)
```

Link `PluginBasedApp` privately to `PluginBasedBuildInfo`. Register `BuildInfoTest`, define `PLUGINBASED_TEST_VERSION="${PROJECT_VERSION}"`, and link Qt6::Test plus `PluginBasedBuildInfo`.

- [ ] **Step 4: Verify the C++ component is GREEN**

Run:

```bash
cmake --build --preset debug --target BuildInfoTest --parallel
ctest --test-dir build -R '^build_info$' --output-on-failure
```

Expected: `build_info` passes and `build/build-info.json` exists.

- [ ] **Step 5: Write executable-level CLI tests before changing main**

Create `tests/build_info_cli_checks.py` accepting `--app`, `--json`, and `--version`. Execute the real binary and assert literal behavior:

```python
version = subprocess.run([args.app, "--version"], text=True,
                         capture_output=True, check=True)
assert version.stdout.startswith(f"PluginBased {args.version} (build ")
assert "compiler" not in version.stdout.casefold()

details = subprocess.run([args.app, "--build-info"], text=True,
                         capture_output=True, check=True)
printed = json.loads(details.stdout)
packaged = json.loads(Path(args.json).read_text(encoding="utf-8"))
assert printed == packaged
assert "gitRef" not in printed
```

Register it as `build_info_cli` with `$<TARGET_FILE:PluginBasedApp>`, `${CMAKE_BINARY_DIR}/build-info.json`, and `${PROJECT_VERSION}`.

- [ ] **Step 6: Run the CLI test and verify RED**

Run:

```bash
cmake --build --preset debug --target PluginBasedApp --parallel
ctest --test-dir build -R '^build_info_cli$' --output-on-failure
```

Expected: FAIL because the current GUI application ignores both options and enters the event loop.

- [ ] **Step 7: Replace hard-coded runtime version strings and implement CLI/log output**

In `app/main.cpp`, scan exact `--version` and `--build-info` arguments before constructing `QGuiApplication`, print `BuildInfo::conciseVersion()` or `BuildInfo::json()`, and return 0. After application construction:

```cpp
app.setApplicationVersion(BuildInfo::productVersion());
```

Replace the literal startup version log with:

```cpp
LOG_INFO("=== {} starting ===", BuildInfo::conciseVersion().toStdString());
LOG_INFO("Build identity:\n{}", BuildInfo::diagnosticSummary().toStdString());
```

Do not read `build-info.json` in the executable.

- [ ] **Step 8: Verify C++ and CLI tests together**

Run:

```bash
cmake --build --preset debug --target PluginBasedApp BuildInfoTest --parallel
ctest --test-dir build -R '^build_info(_cli)?$' --output-on-failure
```

Expected: both tests pass; `--version` is concise and `--build-info` exactly matches the generated JSON.

- [ ] **Step 9: Commit the runtime identity slice**

```bash
git add app/BuildInfo.h app/BuildInfo.cpp app/CMakeLists.txt app/main.cpp tests/tst_build_info.cpp tests/build_info_cli_checks.py CMakeLists.txt
git commit -m "[功能新增] 在应用中暴露构建身份"
```

---

### Task 3: Concise About UI And Native Platform Versions

**Files:**
- Create: `app/qml/AboutDialog.qml`
- Create: `app/version.rc.in`
- Create: `tests/native_version_metadata_checks.py`
- Modify: `app/AppController.h`
- Modify: `app/AppController.cpp`
- Modify: `app/qml/main.qml`
- Modify: `app/CMakeLists.txt`
- Modify: `translations/pluginbased_zh_CN.ts`
- Modify: `CMakeLists.txt` test-registration block

**Interfaces:**
- Consumes: `BuildInfo::productName()`, `productVersion()`, `buildNumber()`, and `diagnosticSummary()`.
- Produces: read-only QML properties `appName`, `appVersion`, `buildNumber`, the `copyBuildDiagnosticInfo()` action, About dialog, and native stable-version metadata.

- [ ] **Step 1: Write a native macOS metadata test against the existing bundle**

Create `tests/native_version_metadata_checks.py` with `--app-bundle` and `--version`. On Darwin, load `Contents/Info.plist` through `plistlib` and assert:

```python
with (bundle / "Contents" / "Info.plist").open("rb") as stream:
    info = plistlib.load(stream)
assert info["CFBundleIdentifier"] == "com.pluginbased.app"
assert info["CFBundleShortVersionString"] == args.version
assert info["CFBundleVersion"] == args.version
```

On non-Darwin hosts, exit successfully because Windows VERSIONINFO is verified by its native CI job. Register `native_version_metadata` with the built app bundle path on Apple.

- [ ] **Step 2: Run the metadata test and verify RED**

Run:

```bash
cmake --build --preset debug --target PluginBasedApp --parallel
python3 tests/native_version_metadata_checks.py \
  --app-bundle build/app/PluginBasedApp.app --version 1.0.0
```

Expected on macOS: FAIL because the existing bundle identifier is `com.yourcompany.PluginBasedApp` and the short version is `1.0`.

- [ ] **Step 3: Route AppController version properties through BuildInfo**

Move `appName()` and `appVersion()` out of the header and add:

```cpp
Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT)

QString appVersion() const;
QString appName() const;
QString buildNumber() const;
Q_INVOKABLE void copyBuildDiagnosticInfo();
```

Implement each getter with `BuildInfo`. Implement clipboard copying with `QGuiApplication::clipboard()->setText(BuildInfo::diagnosticSummary())`; no clipboard object is owned or retained by `AppController`.

- [ ] **Step 4: Add the concise About dialog and translations**

Create `app/qml/AboutDialog.qml` as a modal `Dialog` with only product name, `qsTr("Version %1").arg(AppController.appVersion)`, `qsTr("Build %1").arg(AppController.buildNumber)`, a copy button, and Close. Add it to `qt_add_qml_module`, instantiate it once in `main.qml`, and add one toolbar `IconButton` that opens it.

Add Chinese translations for `About`, `Version %1`, `Build %1`, `Copy diagnostic information`, and `Close`. Do not show commit, tree state, compiler, platform, architecture, Qt version, or ref name directly in the dialog.

- [ ] **Step 5: Generate stable macOS and Windows native version metadata**

Set these target properties:

```cmake
set_target_properties(PluginBasedApp PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.pluginbased.app"
    MACOSX_BUNDLE_BUNDLE_NAME "PluginBased"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)
```

Create `app/version.rc.in` with numeric four-part values and stable strings:

```rc
#include <windows.h>
VS_VERSION_INFO VERSIONINFO
 FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
 PRODUCTVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
 FILETYPE VFT_APP
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "CompanyName", "PluginBased\0"
      VALUE "FileDescription", "PluginBased\0"
      VALUE "FileVersion", "@PROJECT_VERSION@.0\0"
      VALUE "InternalName", "PluginBasedApp\0"
      VALUE "OriginalFilename", "PluginBasedApp.exe\0"
      VALUE "ProductName", "PluginBased\0"
      VALUE "ProductVersion", "@PROJECT_VERSION@.0\0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x0409, 1200
  END
END
```

Configure and add the RC file only under `WIN32`.

- [ ] **Step 6: Build and verify metadata plus QML integration**

Run:

```bash
cmake --preset debug --fresh
cmake --build --preset debug --target PluginBasedApp --parallel
python3 tests/native_version_metadata_checks.py \
  --app-bundle build/app/PluginBasedApp.app --version 1.0.0
ctest --test-dir build -R '^(build_info|build_info_cli|native_version_metadata)$' --output-on-failure
```

Expected: all tests pass and the QML module compilation catches syntax or missing-property errors.

- [ ] **Step 7: Manually verify the user boundary**

Launch:

```bash
./build/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp
```

Verify the About dialog shows only product version and eight-character build number, and that `复制诊断信息` copies the full privacy-safe diagnostic block.

- [ ] **Step 8: Commit UI and native metadata**

```bash
git add app/AppController.h app/AppController.cpp app/qml/AboutDialog.qml app/qml/main.qml app/CMakeLists.txt app/version.rc.in translations/pluginbased_zh_CN.ts tests/native_version_metadata_checks.py CMakeLists.txt
git commit -m "[功能新增] 增加版本关于页与平台元数据"
```

---

### Task 4: Package Build-Identity Copy And Verification

**Files:**
- Modify: `tools/deploy.py`
- Modify: `tools/verify.py`
- Modify: `tests/deploy_packaging_checks.py`

**Interfaces:**
- Consumes: `${build_dir}/build-info.json` generated before packaging and `_read_version(build_dir)`.
- Produces: `build-info.json` at Windows/Linux archive roots, macOS `Contents/Resources`, and verifier errors for missing/malformed/mismatched metadata.

- [ ] **Step 1: Add failing package-copy tests for all platform layouts**

Extend fixture creation so valid build metadata uses literal schema data:

```python
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
```

Write it to `build/build-info.json`, call `copy_runtime_resources`, and assert:

```python
assert (mac_bundle / "Contents" / "Resources" / "build-info.json").is_file()
assert (linux_stage / "build-info.json").is_file()
assert (windows_stage / "build-info.json").is_file()
```

Add tests that `scan_build_info(stage, expected_version="1.0.0")` returns no issues, then returns actionable issues for a missing file, malformed JSON, `schemaVersion != 1`, product version `2.0.0`, an invalid commit, and any forbidden `gitRef` or `sourceDir` key.

Keep the existing platform package tests asserting the final names `PluginBased-1.0.0-macOS.dmg`, `PluginBased-1.0.0-linux-x86_64.tar.gz`, and `PluginBased-1.0.0-win64.zip`, so package naming is checked against the same cached product version.

Use explicit mutations so each failure proves a separate boundary:

```python
assert verifier.scan_build_info(stage, expected_version="1.0.0") == []

document["productVersion"] = "2.0.0"
write(stage / "build-info.json", json.dumps(document))
assert any("2.0.0" in issue and "1.0.0" in issue
           for issue in verifier.scan_build_info(stage, expected_version="1.0.0"))

document["productVersion"] = "1.0.0"
document["gitRef"] = "internal-release-branch"
write(stage / "build-info.json", json.dumps(document))
assert any("gitRef" in issue for issue in verifier.scan_build_info(stage))

(stage / "build-info.json").write_text("{", encoding="utf-8")
assert any("无效构建信息" in issue for issue in verifier.scan_build_info(stage))
```

- [ ] **Step 2: Run package tests and verify RED**

Run:

```bash
python3 tests/deploy_packaging_checks.py
```

Expected: FAIL because build identity is neither copied nor validated and `scan_build_info` does not exist.

- [ ] **Step 3: Copy the generated JSON without regenerating metadata**

In `copy_runtime_resources`, require `build_dir / "build-info.json"`. Copy it to:

```python
build_info_destination = (
    stage_dir / "Contents" / "Resources" / "build-info.json"
    if platform == "macos"
    else stage_dir / "build-info.json"
)
```

Keep plugin manifests, plugin metadata, and themes in their existing runtime layouts. Include the build-info copy in the returned resource count.

- [ ] **Step 4: Implement schema and product-version verification**

In `tools/verify.py`, add:

```python
def build_info_path(stage_dir: Path) -> Path:
    if (stage_dir / "Contents").is_dir():
        return stage_dir / "Contents" / "Resources" / "build-info.json"
    return stage_dir / "build-info.json"


def scan_build_info(stage_dir: Path, expected_version: str | None = None) -> list[str]:
    path = build_info_path(stage_dir)
    if not path.is_file():
        return [f"  缺失构建信息: {path.relative_to(stage_dir)}"]
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return [f"  无效构建信息: {path.relative_to(stage_dir)} ({error})"]

    issues = []
    required_strings = (
        "productName", "productVersion", "displayVersion", "gitCommit",
        "gitShortCommit", "gitTag", "gitTreeState", "buildType",
        "platform", "architecture", "compiler", "qtVersion",
    )
    if document.get("schemaVersion") != 1:
        issues.append("  build-info.json schemaVersion 必须为 1")
    for key in required_strings:
        if not isinstance(document.get(key), str):
            issues.append(f"  build-info.json 字段必须为字符串: {key}")
    for key in ("gitRef", "branch", "hostname", "sourceDir", "remoteUrl"):
        if key in document:
            issues.append(f"  build-info.json 包含禁止字段: {key}")
    if issues:
        return issues

    version = document["productVersion"]
    commit = document["gitCommit"]
    short = document["gitShortCommit"]
    state = document["gitTreeState"]
    if expected_version is not None and version != expected_version:
        issues.append(f"  构建版本不一致: {version} != {expected_version}")
    if state not in {"clean", "dirty", "unknown"}:
        issues.append(f"  无效源码状态: {state}")
    if commit:
        if len(commit) not in {40, 64} or any(char not in "0123456789abcdefABCDEF" for char in commit):
            issues.append("  gitCommit 必须为 40 或 64 位十六进制提交")
        elif short != commit[:8]:
            issues.append("  gitShortCommit 与 gitCommit 不一致")
    elif state != "unknown" or short != "unknown":
        issues.append("  未知 Git 提交必须同时使用 unknown 状态和构建号")
    if not (document["displayVersion"] == version or
            document["displayVersion"].startswith(version + "+")):
        issues.append("  displayVersion 与 productVersion 不一致")
    return issues
```

Add `--expected-version` to the verifier CLI. Update `deploy.verify_staging` to pass `_read_version(build_dir)` and combine `scan_build_info` issues with existing dependency/resource issues. Preserve standalone verifier behavior when `--expected-version` is omitted.

- [ ] **Step 5: Run package tests and verify GREEN**

Run:

```bash
python3 tests/deploy_packaging_checks.py
```

Expected: all packaging tests pass, including all three destinations and every rejection case.

- [ ] **Step 6: Rebuild and run focused CTest coverage**

Run:

```bash
cmake --build --preset debug --parallel
ctest --test-dir build -R '^(build_info|build_info_cli|deploy_packaging_checks)$' --output-on-failure
```

Expected: focused C++/CLI/package tests pass.

- [ ] **Step 7: Commit packaging integration**

```bash
git add tools/deploy.py tools/verify.py tests/deploy_packaging_checks.py
git commit -m "[功能修改] 校验发布包构建身份"
```

---

### Task 5: Documentation And End-To-End Verification

**Files:**
- Modify: `README.md`
- Modify: `BUILD.md`
- Modify: `tests/ci_ctest_checks.py` only for project-wide version-source invariants not covered by runtime tests

**Interfaces:**
- Consumes: all earlier build identity, CLI, package, and official-build interfaces.
- Produces: documented support workflow and a verified baseline ready for the cross-platform GitHub Actions implementation.

- [ ] **Step 1: Add the remaining project-level invariant test**

Extend `tests/ci_ctest_checks.py` narrowly to enforce that the old application literals cannot become a second version source:

```python
main_cpp = read("app/main.cpp")
app_controller_h = read("app/AppController.h")
require('setApplicationVersion("1.0.0")' not in main_cpp and
        'QStringLiteral("1.0.0")' not in app_controller_h,
        "runtime application version should come from BuildInfo")
```

Do not duplicate generator behavior or JSON schema tests with additional source-text checks.

- [ ] **Step 2: Run the invariant test before documentation edits**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: PASS if Task 2 removed the literals. If it fails, fix the production source rather than weakening the invariant.

- [ ] **Step 3: Document release-version and support contracts**

Update `README.md` with a concise support section:

```text
关于窗口只显示产品版本和构建号。报告问题时点击“复制诊断信息”；
也可运行 PluginBasedApp --version 或读取发布包中的 build-info.json。
```

Update `BUILD.md` to document:

- edit only `project(PluginBased VERSION 1.0.0)` when advancing to the next product version;
- `./package.sh --version` reads that version;
- generated file locations and ignored/generated status;
- local dirty/unknown behavior;
- official configure flags `-DPLUGINBASED_OFFICIAL_BUILD=ON` and `-DPLUGINBASED_EXPECTED_TAG=v1.0.0`;
- branch names, machine identity, paths, and remotes are never persisted;
- the later GitHub release workflow will use the same strict validator.

- [ ] **Step 4: Run fresh Debug configure/build and full tests**

Run:

```bash
cmake --preset debug --fresh
cmake --build --preset debug --parallel
ctest --preset debug
```

Expected: configure succeeds, all targets build, all CTest tests pass, and `build/build-info.json` matches `PluginBasedApp --build-info`.

- [ ] **Step 5: Run fresh Release configure/build and full tests**

Run:

```bash
cmake --preset release --fresh
cmake --build --preset release --parallel
ctest --preset release
```

Expected: Release configure/build succeeds and the complete Release test suite passes.

- [ ] **Step 6: Create and verify a real local package**

Run:

```bash
./package.sh --skip-build
```

Then inspect the produced platform archive and verify:

```bash
python3 tools/verify.py \
  --stage-dir build-release/_package_macos/PluginBasedApp.app \
  --expected-version 1.0.0
```

Expected on macOS: verified DMG is created; the staged bundle contains `Contents/Resources/build-info.json`; Info.plist, executable CLI output, JSON, and DMG name all report product version `1.0.0` and the current eight-character commit.

- [ ] **Step 7: Review generated metadata for privacy and consistency**

Run:

```bash
./build-release/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp --version
./build-release/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp --build-info
```

Confirm no output contains the current branch name, user name, host name, repository path, or remote URL. Confirm a dirty working tree is explicitly marked and therefore cannot be mistaken for an official build.

- [ ] **Step 8: Commit documentation and the final invariant**

```bash
git add README.md BUILD.md tests/ci_ctest_checks.py
git commit -m "[文档更新] 说明构建版本诊断流程"
```

- [ ] **Step 9: Resume the approved cross-platform Workflow work**

Use `docs/superpowers/specs/2026-08-09-github-cross-platform-release-workflow-design.md` as the source of truth. The Workflow implementation must pass `PLUGINBASED_OFFICIAL_BUILD=ON` and `PLUGINBASED_EXPECTED_TAG=${GITHUB_REF_NAME}` only for `v*` tag builds, then publish the already verified package and checksum artifacts.
