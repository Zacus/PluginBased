# Package Release Preset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `package.sh` configure, build, and package the `release` CMake Preset by default without breaking caller-managed Release build directories.

**Architecture:** `package.sh` owns the mapping from the default packaging workflow to the `release` Preset and `build-release/`. Positional custom directories remain supported, but both default and custom paths pass a cache-level Release validation before deployment; `tools/deploy.py` remains build-directory driven.

**Tech Stack:** Bash, CMake Presets schema 3, Python 3 packaging regression checks, CTest, Markdown.

## Global Constraints

- Preserve all existing `package.sh` options and the positional custom-build-directory argument.
- Default packaging uses Preset `release` and `${sourceDir}/build-release`.
- Never deploy a cache that is neither single-config Release nor multi-config with Release available.
- Preserve macOS, Linux, and Windows deployment layouts and package formats.
- Treat `QT_DIR` as the explicit override and `QT_ROOT` as its fallback.
- Do not modify or stage unrelated playback/Seek work.

---

### Task 1: Lock the Release packaging behavior with executable regression tests

**Files:**
- Modify: `tests/deploy_packaging_checks.py`

**Interfaces:**
- Consumes: the public `./package.sh [options] [build-directory]` CLI.
- Produces: regression coverage for default Preset invocation, custom multi-config builds, Debug rejection, and `QT_ROOT` fallback.

- [ ] **Step 1: Add fixture helpers and failing behavior tests**

Add helpers that copy `package.sh` and a minimal `tools/package.yml` into a temporary project, create a fake `cmake` executable that logs arguments, and invoke the copied script. Add these tests:

```python
def make_package_fixture(root: Path) -> Path:
    script = root / "package.sh"
    shutil.copy2(ROOT / "package.sh", script)
    write(root / "tools" / "package.yml", "{}\n")
    return script


def package_environment(fake_bin=None):
    environment = os.environ.copy()
    if fake_bin:
        environment["PATH"] = f"{fake_bin}{os.pathsep}{environment['PATH']}"
    environment.pop("QT_DIR", None)
    return environment


def make_fake_cmake(root: Path):
    fake_bin = root / "fake-bin"
    log = root / "cmake.log"
    fake = fake_bin / "cmake"
    write(fake, """#!/usr/bin/env bash
set -euo pipefail
printf '%s\\n' "$*" >> "${PACKAGE_TEST_CMAKE_LOG}"
if [[ "${1:-}" == "--preset" ]]; then
    mkdir -p build-release
    printf 'CMAKE_BUILD_TYPE:STRING=Release\\n' > build-release/CMakeCache.txt
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

        result = subprocess.run(
            [str(script), "--build-only"], cwd=root,
            text=True, capture_output=True, env=environment)

        assert result.returncode == 0, result.stderr
        calls = log.read_text(encoding="utf-8").splitlines()
        assert calls[0] == "--preset release"
        assert calls[1].startswith("--build --preset release --parallel ")


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


def test_package_script_uses_qt_root_as_deployment_fallback() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        script = make_package_fixture(root)
        write(root / "build-release" / "CMakeCache.txt",
              "CMAKE_BUILD_TYPE:STRING=Release\n")
        write(root / "tools" / "deploy.py",
              "import sys\nprint('\\n'.join(sys.argv[1:]))\n")
        environment = package_environment()
        environment["QT_ROOT"] = "/opt/Qt/6.8.3/macos"

        result = subprocess.run(
            [str(script), "--skip-build", "--no-verify"], cwd=root,
            text=True, capture_output=True, env=environment)

        assert result.returncode == 0, result.stderr
        assert "--qt-dir\n/opt/Qt/6.8.3/macos" in result.stdout
```

Call all four tests from `main()`.

- [ ] **Step 2: Run the regression test and verify RED**

Run:

```bash
python3 tests/deploy_packaging_checks.py
```

Expected: FAIL because the current script still defaults to `build/`, never invokes `cmake --preset release`, accepts Debug caches, and does not inherit `QT_ROOT`.

- [ ] **Step 3: Commit the failing tests separately**

```bash
git add tests/deploy_packaging_checks.py
git diff --cached --check
git commit -m "[测试] 约束 Release Preset 打包入口"
```

---

### Task 2: Implement the Preset-first Release entry point

**Files:**
- Modify: `package.sh`

**Interfaces:**
- Consumes: `QT_DIR`, `QT_ROOT`, the root `release` configure/build Presets, and an optional positional custom build directory.
- Produces: a validated absolute `BUILD_DIR` passed unchanged to `tools/deploy.py`.

- [ ] **Step 1: Change defaults and track custom-directory mode**

Use these defaults:

```bash
BUILD_PRESET="release"
BUILD_DIR="${SCRIPT_DIR}/build-release"
CUSTOM_BUILD_DIR=false
QT_DIR="${QT_DIR:-${QT_ROOT:-}}"
```

When parsing a positional directory, set `CUSTOM_BUILD_DIR=true` after resolving it.

- [ ] **Step 2: Add Release-cache validation**

Add a helper that reads `CMAKE_BUILD_TYPE` and `CMAKE_CONFIGURATION_TYPES` from `CMakeCache.txt`. Accept exactly `CMAKE_BUILD_TYPE=Release`, or a semicolon-delimited configuration list containing `Release`; otherwise terminate with an actionable error before deployment.

```bash
validate_release_cache() {
    local cache="${BUILD_DIR}/CMakeCache.txt"
    [[ -f "${cache}" ]] || die "构建目录未初始化: ${BUILD_DIR}"

    local build_type config_types
    build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:[^=]*=//p' "${cache}" | head -1)"
    config_types="$(sed -n 's/^CMAKE_CONFIGURATION_TYPES:[^=]*=//p' "${cache}" | head -1)"

    if [[ "${build_type}" != "Release" && ";${config_types};" != *";Release;"* ]]; then
        die "打包要求 Release 构建目录: ${BUILD_DIR}"
    fi
}
```

- [ ] **Step 3: Replace the build flow**

For the default path, configure and build from `SCRIPT_DIR` through Presets:

```bash
(
    cd "${SCRIPT_DIR}"
    cmake --preset "${BUILD_PRESET}"
)
validate_release_cache
(
    cd "${SCRIPT_DIR}"
    cmake --build --preset "${BUILD_PRESET}" --parallel "${JOBS}"
)
```

For a custom directory, require its cache, validate it, and build with:

```bash
cmake --build "${BUILD_DIR}" --config Release --parallel "${JOBS}"
```

In `--skip-build` mode, skip configure/build but still call `validate_release_cache` before deployment.

- [ ] **Step 4: Run focused checks and verify GREEN**

Run:

```bash
bash -n package.sh
python3 tests/deploy_packaging_checks.py
```

Expected: both commands PASS.

- [ ] **Step 5: Commit the implementation**

```bash
git add package.sh
git diff --cached --check
git commit -m "[功能修改] 使用 Release Preset 构建发布包"
```

---

### Task 3: Align packaging documentation and run integration verification

**Files:**
- Modify: `BUILD.md`
- Verify: `package.sh`
- Verify: `CMakePresets.json`
- Verify: `tests/deploy_packaging_checks.py`

**Interfaces:**
- Consumes: the Task 2 default and custom-directory behavior.
- Produces: copy-pasteable default, skip-build, and custom Release packaging commands.

- [ ] **Step 1: Update the packaging examples**

Document:

```bash
# 自动 configure/build release Preset 并打包
./package.sh

# 已完成 release Preset 构建，只执行打包
./package.sh --skip-build

# 打包调用方管理的 Release 构建目录
./package.sh --skip-build ./build-release --qt-dir "$QT_ROOT"
```

State that the default build directory is `build-release/`, custom trees must be Release-capable, and `QT_ROOT` is accepted when `QT_DIR` is unset.

- [ ] **Step 2: Run focused and full verification**

Run:

```bash
bash -n package.sh
python3 -m json.tool CMakePresets.json >/dev/null
cmake --list-presets
python3 tests/deploy_packaging_checks.py
python3 tests/ci_ctest_checks.py
ctest --preset debug --output-on-failure
git diff --check
```

Expected: shell syntax, Preset parsing, both focused Python checks, and all registered Debug tests PASS.

- [ ] **Step 3: Commit documentation and audit scope**

```bash
git add BUILD.md
git diff --cached --check
git commit -m "[文档更新] 说明 Release Preset 打包流程"
git status --short
git log -5 --oneline
```

Expected: only Task 1-3 files appear in the new commits; unrelated playback/Seek changes remain unstaged.
