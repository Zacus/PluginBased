# Qt Official Binary, vcpkg, and CMake Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the project on Qt 6.8.3 official binaries, keep non-Qt dependencies pinned in vcpkg, and make CMake Presets the shared local/CI build interface.

**Architecture:** `CMakePresets.json` owns reproducible configure/build/test arguments and consumes only `VCPKG_ROOT` and `QT_ROOT` from the caller. macOS CI installs the exact official Qt kit with pinned `aqtinstall`, then invokes the same Debug presets used locally; vcpkg retains ownership of spdlog, FFmpeg, and pkgconf.

**Tech Stack:** CMake 3.21 Presets schema 3, Qt 6.8.3, vcpkg manifest mode, GitHub Actions macOS 14, Python 3.12, aqtinstall 3.3.0, Python configuration checks, CTest.

## Global Constraints

- Require Qt `6.8.3` exactly; keep `qt_standard_project_setup(REQUIRES 6.8)` as the Qt policy baseline.
- Keep vcpkg at `ea1a7396b05637a53bf23c078647ecc0edee4b80` with full checkout history.
- Preserve `spdlog 1.17.0#0`, `ffmpeg 8.0.1#2`, `pkgconf 2.5.1#4`, and all current vcpkg features.
- Keep C++17, public API/ABI, plugin behavior, build target structure, packaging behavior, and the current macOS-only CI platform unchanged.
- Do not add Qt to `vcpkg.json`; do not add Conan, system/Homebrew Qt fallback, or a third-party Qt installer action.
- Preserve the existing Debug output at `build/` and Release output at `build-release/`.
- Do not modify, stage, or commit the existing playback/Seek work except for the isolated Qt version hunk at the top of `CMakeLists.txt`.

## File Structure

- Create `CMakePresets.json`: committed configure/build/test contract for Debug and Release.
- Modify `CMakeLists.txt`: enforce exact Qt 6.8.3 while leaving the Qt policy baseline and all targets unchanged.
- Modify `tests/ci_ctest_checks.py`: validate Preset semantics, the exact Qt contract, and macOS CI dependency/build behavior.
- Modify `.github/workflows/ci.yml`: install/cache/validate official Qt 6.8.3 and consume Debug presets.
- Modify `README.md`: expose the short supported setup and common build commands.
- Modify `BUILD.md`: document platform environment paths, Debug/Release presets, direct diagnostic configure, packaging, and coordinated upgrade rules.

---

### Task 1: Add the exact Qt and CMake Presets contract

**Files:**
- Create: `CMakePresets.json`
- Modify: `CMakeLists.txt:9`
- Modify: `tests/ci_ctest_checks.py:19-60`
- Test: `tests/ci_ctest_checks.py`

**Interfaces:**
- Consumes: caller environment variables `VCPKG_ROOT` and `QT_ROOT`.
- Produces: configure/build/test presets named `debug` and `release`; `debug` configures `${sourceDir}/build`, and `release` configures `${sourceDir}/build-release`.

- [ ] **Step 1: Write the failing Presets and Qt contract checks**

Add this logic after the existing expected vcpkg version constants in `main()`:

```python
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
```

- [ ] **Step 2: Run the focused check and verify RED**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: FAIL with `CMakePresets.json should define the supported build interface` because the Presets file does not exist yet.

- [ ] **Step 3: Add the minimal Presets implementation**

Create `CMakePresets.json` with this complete content:

```json
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 21,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
      "cacheVariables": {
        "CMAKE_PREFIX_PATH": {
          "type": "PATH",
          "value": "$env{QT_ROOT}"
        },
        "Qt6_DIR": {
          "type": "PATH",
          "value": "$env{QT_ROOT}/lib/cmake/Qt6"
        },
        "BUILD_TESTING": true
      }
    },
    {
      "name": "debug",
      "displayName": "Debug",
      "inherits": "base",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "release",
      "displayName": "Release",
      "inherits": "base",
      "binaryDir": "${sourceDir}/build-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "debug",
      "configurePreset": "debug",
      "configuration": "Debug"
    },
    {
      "name": "release",
      "configurePreset": "release",
      "configuration": "Release"
    }
  ],
  "testPresets": [
    {
      "name": "debug",
      "configurePreset": "debug",
      "configuration": "Debug",
      "output": {
        "outputOnFailure": true
      }
    },
    {
      "name": "release",
      "configurePreset": "release",
      "configuration": "Release",
      "output": {
        "outputOnFailure": true
      }
    }
  ]
}
```

Change only the Qt package line at the top of `CMakeLists.txt`:

```cmake
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui GuiPrivate Quick QuickControls2 Multimedia ShaderTools Test LinguistTools)
```

- [ ] **Step 4: Validate JSON, list Presets, and verify GREEN**

Run:

```bash
python3 -m json.tool CMakePresets.json >/dev/null
cmake --list-presets
python3 tests/ci_ctest_checks.py
```

Expected: JSON validation and Preset listing PASS; the Python check advances past all new Presets/Qt assertions and still passes against the unchanged CI assertions.

- [ ] **Step 5: Commit only the Task 1 changes**

Stage `CMakePresets.json` and `tests/ci_ctest_checks.py` normally. Stage only the one-line Qt hunk from `CMakeLists.txt`, excluding the existing Seek test hunk, then verify with `git diff --cached` before committing:

```bash
git commit -m "[功能修改] 统一 CMake Presets 构建入口"
```

### Task 2: Replace Homebrew Qt CI setup with pinned official Qt

**Files:**
- Modify: `.github/workflows/ci.yml:13-190`
- Modify: `tests/ci_ctest_checks.py:198-220`
- Test: `tests/ci_ctest_checks.py`

**Interfaces:**
- Consumes: `CMakePresets.json`, fixed vcpkg checkout, GitHub runner contexts `${{ github.workspace }}` and `${{ runner.temp }}`.
- Produces: a validated Qt installation at `${{ runner.temp }}/Qt/6.8.3/macos` and a CI build driven by the `debug` configure/build/test presets.

- [ ] **Step 1: Replace the old CI assertions with the approved behavior checks**

Keep the existing vcpkg checkout and QtQuickComponents assertions. Replace assertions for direct CMake commands with:

```python
    require("VCPKG_ROOT: ${{ github.workspace }}/vcpkg" in workflow,
            "CI should expose the fixed vcpkg checkout to CMake Presets")
    require("QT_ROOT: ${{ runner.temp }}/Qt/6.8.3/macos" in workflow,
            "CI should expose the exact official Qt kit to CMake Presets")
    require("actions/setup-python@v5" in workflow and "python-version: '3.12'" in workflow,
            "CI should provide the pinned Python line used by aqtinstall")
    require("aqtinstall==3.3.0" in workflow,
            "CI should pin the Qt installer version")
    require('aqt install-qt --outputdir "${RUNNER_TEMP}/Qt" mac desktop 6.8.3 clang_64 -m qtmultimedia qtshadertools' in workflow,
            "CI should install only the required Qt 6.8.3 official modules")
    require("brew install qt" not in workflow and "brew reinstall qt" not in workflow,
            "CI should not fall back to Homebrew Qt")
    require('test -f "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake"' in workflow,
            "CI should validate restored or installed Qt before configuring")
    require("cmake --preset debug" in workflow,
            "CI should configure through the shared Debug preset")
    require("cmake --build --preset debug --parallel" in workflow,
            "CI should build through the shared Debug preset")
    require("ctest --preset debug" in workflow,
            "CI should test through the shared Debug preset")
```

- [ ] **Step 2: Run the focused check and verify RED**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: FAIL with `CI should expose the fixed vcpkg checkout to CMake Presets` because the workflow still uses direct arguments and Homebrew Qt.

- [ ] **Step 3: Implement the pinned official Qt CI flow**

In the macOS job environment, add:

```yaml
      VCPKG_ROOT: ${{ github.workspace }}/vcpkg
      QT_ROOT: ${{ runner.temp }}/Qt/6.8.3/macos
```

Remove `HOMEBREW_NO_AUTO_UPDATE` and the Homebrew `Install Qt` script. Add these steps after the vcpkg checkout:

```yaml
      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.12'

      - name: Install aqtinstall
        run: python -m pip install --disable-pip-version-check aqtinstall==3.3.0

      - name: Cache Qt
        id: qt-cache
        uses: actions/cache@v4
        with:
          path: ${{ runner.temp }}/Qt/6.8.3
          key: qt-${{ runner.os }}-6.8.3-clang_64-qtmultimedia-qtshadertools-aqt-3.3.0

      - name: Install Qt 6.8.3
        if: steps.qt-cache.outputs.cache-hit != 'true'
        run: aqt install-qt --outputdir "${RUNNER_TEMP}/Qt" mac desktop 6.8.3 clang_64 -m qtmultimedia qtshadertools

      - name: Validate Qt 6.8.3
        run: test -f "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake"
```

Change only the configure invocation inside the existing diagnostic wrapper:

```bash
          cmake --preset debug 2>&1 | tee configure.log || {
```

Change diagnostic environment output to print `VCPKG_ROOT` and `QT_ROOT`. Replace build and test invocations with:

```bash
cmake --build --preset debug --parallel
ctest --preset debug
```

Keep `set -o pipefail`, failure annotations, summaries, uploaded file paths, vcpkg caches, bootstrap, and full-history checkout unchanged.

- [ ] **Step 4: Verify the CI contract is GREEN**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: PASS, including unchanged vcpkg baseline/override/full-history assertions and all new official Qt/Preset assertions.

- [ ] **Step 5: Commit Task 2**

```bash
git add .github/workflows/ci.yml tests/ci_ctest_checks.py
git diff --cached --check
git commit -m "[功能修改] 固定 CI Qt 官方工具链"
```

### Task 3: Document the supported local and CI workflow

**Files:**
- Modify: `README.md:60-135`
- Modify: `BUILD.md:1-115`
- Modify: `BUILD.md:135-155`

**Interfaces:**
- Consumes: the environment and preset names implemented in Tasks 1-2.
- Produces: copyable macOS/Linux/Windows setup examples, Debug/Release commands, a direct diagnostic configure command, and an exact packaging path example.

- [ ] **Step 1: Update README dependency and quick-build guidance**

Make the dependency table and surrounding text state that Qt is exactly `6.8.3`, obtained from the official Qt installer with `qtmultimedia` and `qtshadertools`, while vcpkg owns only the existing non-Qt dependencies. Replace the quick-build configure/build/test commands with:

```bash
export VCPKG_ROOT=/path/to/vcpkg
export QT_ROOT="$HOME/Qt/6.8.3/macos"

cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Keep the application run command at `./build/app/PluginBasedApp`, and state that CI installs the same Qt 6.8.3 official kit before invoking the Debug presets.

- [ ] **Step 2: Rewrite BUILD dependency installation and Preset sections**

Document the exact Qt 6.8.3 official kit and modules first, followed by the existing fixed vcpkg checkout. Provide these environment examples:

```bash
# macOS
export VCPKG_ROOT=/path/to/vcpkg
export QT_ROOT="$HOME/Qt/6.8.3/macos"

# Linux
export VCPKG_ROOT=/path/to/vcpkg
export QT_ROOT="$HOME/Qt/6.8.3/gcc_64"
```

```powershell
# Windows / PowerShell
$env:VCPKG_ROOT = "C:\src\vcpkg"
$env:QT_ROOT = "C:\Qt\6.8.3\msvc2022_64"
```

Replace Debug and Release build sections with:

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug

cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

Keep one explicit troubleshooting command that passes the exact toolchain, prefix, and Qt config directory. Change every packaging Qt path example from 6.8.0 to 6.8.3. Record the coordinated Qt upgrade set: `CMakeLists.txt`, CI install command/cache key/`QT_ROOT`, regression checks, and docs.

- [ ] **Step 3: Verify documentation against committed configuration**

Run:

```bash
rg -n "6\.8\.0|6\.8\+|brew install qt|系统包管理器安装" README.md BUILD.md
rg -n "cmake --preset (debug|release)|cmake --build --preset (debug|release)|ctest --preset (debug|release)|VCPKG_ROOT|QT_ROOT|6\.8\.3" README.md BUILD.md
```

Expected: the first command finds no stale Qt version/fallback guidance; the second command shows both environment variables, exact Qt 6.8.3, and the documented preset entry points.

- [ ] **Step 4: Commit Task 3**

```bash
git add README.md BUILD.md
git diff --cached --check
git commit -m "[功能修改] 记录 Qt 与 Presets 构建流程"
```

### Task 4: Run integration verification and protect unrelated work

**Files:**
- Verify: `CMakePresets.json`
- Verify: `CMakeLists.txt`
- Verify: `.github/workflows/ci.yml`
- Verify: `tests/ci_ctest_checks.py`
- Verify: `README.md`
- Verify: `BUILD.md`

**Interfaces:**
- Consumes: all deliverables from Tasks 1-3 and any locally available exact Qt/vcpkg installations.
- Produces: evidence that static contracts, Preset parsing, current build outputs, and—when available—the exact Qt configure/build/test path work without including unrelated playback/Seek changes.

- [ ] **Step 1: Run focused static and Preset checks**

```bash
python3 -m json.tool CMakePresets.json >/dev/null
cmake --list-presets
python3 tests/ci_ctest_checks.py
git diff --check
```

Expected: all commands PASS; `cmake --list-presets` lists `debug` and `release`.

- [ ] **Step 2: Detect an exact local toolchain without mutating the workspace**

Inspect the current build cache and environment:

```bash
rg -n "^(Qt6_DIR|CMAKE_PREFIX_PATH|CMAKE_TOOLCHAIN_FILE):" build/CMakeCache.txt
test -f "${QT_ROOT:-/nonexistent}/lib/cmake/Qt6/Qt6Config.cmake"
test -f "${VCPKG_ROOT:-/nonexistent}/scripts/buildsystems/vcpkg.cmake"
```

If the cache or environment resolves Qt 6.8.3 and the fixed vcpkg checkout, export the corresponding `QT_ROOT` and `VCPKG_ROOT` and continue with Step 3. Otherwise skip only the fresh configure/build path and record the missing exact kit in the final report.

- [ ] **Step 3: Run the exact Debug Preset path when dependencies are available**

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug
```

Expected: configure, full build, and all registered tests PASS with Qt 6.8.3 and the fixed vcpkg toolchain.

- [ ] **Step 4: Run the existing configured CTest suite when fresh configure is unavailable**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all currently registered tests PASS. This verifies the configuration checks without claiming that the exact official Qt 6.8.3 install path was exercised locally.

- [ ] **Step 5: Audit the final scope and commit state**

```bash
git status --short
git log -5 --oneline
git show --stat --oneline HEAD
```

Expected: implementation commits include only `CMakePresets.json`, the isolated Qt version hunk, CI/configuration tests, README, and BUILD. Existing playback/Seek modifications remain unstaged unless they were already staged by the user before this plan began.

## Integration Verification

- Required focused checks: JSON parser, `cmake --list-presets`, `tests/ci_ctest_checks.py`, and `git diff --check`.
- Required broader check: full existing CTest suite from `build/`.
- Conditional end-to-end check: fresh Debug configure/build/CTest through Presets when exact Qt 6.8.3 and fixed vcpkg are locally available.
- CI end-to-end contract: macOS 14 restores or installs official Qt 6.8.3, validates `Qt6Config.cmake`, then runs all three Debug preset commands.
- Packaging is documentation-only in this change; no package artifacts are generated unless a Release build and deployment environment are already available.

## Completion Definition

The work is complete when exact Qt 6.8.3 is enforced, Debug/Release Presets parse and map to the established build directories, macOS CI contains no Homebrew Qt fallback and uses pinned aqtinstall plus official Qt modules, documentation matches the executable configuration, focused checks and the available broader suite pass, and unrelated playback/Seek work remains outside these commits.
