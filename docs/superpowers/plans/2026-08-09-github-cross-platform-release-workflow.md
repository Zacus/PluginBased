# GitHub Cross-Platform Release Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the macOS-only workflow with one event-aware macOS/Linux/Windows matrix that validates pull requests and produces verified, checksummed packages and tag releases.

**Architecture:** `.github/workflows/ci.yml` owns orchestration only and calls the repository's `debug`/`release` CMake Presets plus `package.sh`. `tests/ci_ctest_checks.py` locks the event, matrix, permission, version, retention, diagnostics, and release-publication contracts before the workflow is rewritten.

**Tech Stack:** GitHub Actions, CMake Presets, CTest, Ninja, vcpkg manifest mode, aqtinstall 3.3.0, Qt 6.8.3, Python 3.12, Bash/PowerShell, GitHub CLI.

## Global Constraints

- Use `macos-14`, `ubuntu-22.04`, and `windows-2022` with matrix `fail-fast: false`.
- Use Qt 6.8.3 kits `clang_64`/`macos`, `gcc_64`/`gcc_64`, and `win64_msvc2022_64`/`msvc2022_64`.
- Pin vcpkg to `ea1a7396b05637a53bf23c078647ecc0edee4b80`, Python to 3.12, and aqtinstall to 3.3.0.
- Pull requests run fresh Debug configure/build/CTest and never package.
- `main`, manual, and `v*` runs use fresh Release configure/build/CTest and the repository packaging entry point.
- Tag builds require `vMAJOR.MINOR.PATCH`, exact equality with `./package.sh --version`, `PLUGINBASED_OFFICIAL_BUILD=ON`, and `PLUGINBASED_EXPECTED_TAG=${GITHUB_REF_NAME}`.
- Ordinary packages retain 30 days; tag workflow artifacts retain 365 days; diagnostics retain 30 days.
- Generate one SHA256 file per package and fail on zero or multiple package matches.
- Default permissions are `contents: read`; only the tag publication job has `contents: write`.
- Do not add signing, notarization, AppImage, DEB, RPM, installer EXE, external artifact storage, or package registries.

---

### Task 1: Encode The Cross-Platform Workflow Contract

**Files:**
- Modify: `tests/ci_ctest_checks.py`

**Interfaces:**
- Consumes: the approved workflow design and workflow text at `.github/workflows/ci.yml`.
- Produces: failing source-level assertions that define the exact matrix, event, release, and least-privilege contract.

- [ ] **Step 1: Add workflow trigger and concurrency assertions**

Import `re`, then require the workflow to contain tag trigger `tags:`, `- 'v*'`, top-level `contents: read`, a `concurrency` group, and a `cancel-in-progress` expression that is false for `refs/tags/`.

```python
require("tags:" in workflow and "- 'v*'" in workflow,
        "CI should build version tags")
require("permissions:\n  contents: read" in workflow,
        "workflow permissions should default to read-only contents")
require("concurrency:" in workflow and
        "!startsWith(github.ref, 'refs/tags/')" in workflow,
        "CI should cancel stale branch runs but preserve tag releases")
```

- [ ] **Step 2: Add matrix and event-profile assertions**

Require one `build_and_package` job, `fail-fast: false`, the three runner/Qt/package tuples, explicit Debug-vs-Release expressions, a finite timeout, Linux prerequisites, and MSVC environment initialization.

```python
for required in (
    "runner: macos-14", "qt_host: mac", "qt_arch: clang_64",
    "qt_kit: macos", "package_glob: '*.dmg'",
    "runner: ubuntu-22.04", "qt_host: linux", "qt_arch: gcc_64",
    "qt_kit: gcc_64", "package_glob: '*.tar.gz'",
    "runner: windows-2022", "qt_host: windows",
    "qt_arch: win64_msvc2022_64", "qt_kit: msvc2022_64",
    "package_glob: '*.zip'",
):
    require(required in workflow, f"CI matrix missing {required}")
require("fail-fast: false" in workflow and "timeout-minutes: 180" in workflow,
        "matrix jobs should be independent and bounded")
require("github.event_name == 'pull_request' && 'debug' || 'release'" in workflow,
        "pull requests should use Debug and package events should use Release")
require("ilammy/msvc-dev-cmd@v1" in workflow,
        "Windows should initialize the VS 2022 compiler environment")
require("patchelf" in workflow,
        "Linux should install its required packaging tool")
```

- [ ] **Step 3: Add tag, artifact, diagnostics, and release assertions**

Require strict tag comparison, official CMake flags, PR packaging exclusion, checksum generation, 30/365 retention, matrix-specific diagnostics, tag-only release dependency, release-only write permission, artifact download, checksum revalidation, and non-overwriting `gh release create` publication.

```python
for required in (
    "${GITHUB_REF_NAME#v}", "./package.sh --version",
    "-DPLUGINBASED_OFFICIAL_BUILD=ON",
    "-DPLUGINBASED_EXPECTED_TAG=${GITHUB_REF_NAME}",
    "if: github.event_name != 'pull_request'",
    "hashlib.sha256", "retention-days:", "365", "30",
    "if: failure()", "if-no-files-found: ignore",
    "name: release", "needs: build_and_package",
    "contents: write", "actions/download-artifact@v4",
    "gh release view", "gh release create", "--verify-tag",
):
    require(required in workflow, f"release workflow missing {required}")
```

- [ ] **Step 4: Run the focused contract test and verify RED**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: fail at the first new tag/matrix assertion because the current workflow is macOS-only and has no tag publication job.

---

### Task 2: Implement The Event-Aware Build And Package Matrix

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `debug` and `release` configure/build/test Presets, `package.sh --version`, `package.sh --skip-build`, and strict official-build CMake cache variables.
- Produces: three independent native jobs and artifacts named `PluginBased-<platform>-package`.

- [ ] **Step 1: Define events, permissions, concurrency, and matrix**

Set push branches to `main`, push tags to `'v*'`, keep pull request/manual triggers, default `contents: read`, and use:

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}
  cancel-in-progress: ${{ !startsWith(github.ref, 'refs/tags/') }}
```

Create `build_and_package` with `timeout-minutes: 180`, `fail-fast: false`, and three `include` entries carrying `platform`, `runner`, `qt_host`, `qt_arch`, `qt_kit`, and `package_glob`.

- [ ] **Step 2: Prepare native toolchains and pinned dependencies**

Use app and vcpkg checkouts, Python 3.12, aqtinstall 3.3.0, OS/Qt-architecture cache keys, explicit `${{ runner.temp }}` vcpkg download/binary cache directories, `ilammy/msvc-dev-cmd@v1` on Windows, Linux apt prerequisites including `patchelf`, and platform-specific vcpkg bootstrap commands.

Validate CMake, Ninja, Python, vcpkg, and all three required Qt CMake package files after cache restore/install.

- [ ] **Step 3: Implement event-aware configure, build, and CTest**

Set:

```yaml
BUILD_PRESET: ${{ github.event_name == 'pull_request' && 'debug' || 'release' }}
BUILD_DIR: ${{ github.event_name == 'pull_request' && 'build' || 'build-release' }}
```

Before tagged configuration, validate the tag regex and compare `${GITHUB_REF_NAME#v}` with `./package.sh --version`. Configure tagged runs with strict official flags; configure other runs with the selected preset. Write configure/CTest console logs under `${RUNNER_TEMP}` so an untracked log cannot make a tagged source tree dirty.

- [ ] **Step 4: Package and generate a checksum for non-PR events**

Run `./package.sh --skip-build`, then use Python's `pathlib` and `hashlib.sha256` to require exactly one file matching `matrix.package_glob`, stream-hash it, and create `<package>.sha256` containing `<digest>  <filename>`.

Upload the package and checksum as `PluginBased-${{ matrix.platform }}-package`; set retention to 365 for `refs/tags/v*` and 30 otherwise.

- [ ] **Step 5: Upload matrix-specific failure diagnostics**

With `if: failure()`, upload the configure log, CTest log, vcpkg manifest install log, `CMakeConfigureLog.yaml`, `CMakeError.log`, and `LastTest.log` as `PluginBased-${{ matrix.platform }}-diagnostics`, `if-no-files-found: ignore`, retention 30.

---

### Task 3: Publish Verified Tag Assets

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: the three `PluginBased-<platform>-package` artifacts and `github.ref_name`.
- Produces: one GitHub Release containing three packages and three SHA256 files without rebuilding them.

- [ ] **Step 1: Add the tag-only release job**

Add job `release` with `if: startsWith(github.ref, 'refs/tags/v')`, `needs: build_and_package`, `ubuntu-22.04`, `timeout-minutes: 15`, and job-level `contents: write`.

- [ ] **Step 2: Download and validate release assets**

Use `actions/download-artifact@v4` with pattern `PluginBased-*-package`, `merge-multiple: true`, and `release-assets/`. Use Python to require exactly one `.dmg`, `.tar.gz`, and `.zip`, require each adjacent `.sha256`, recompute every digest, and reject unexpected/missing assets.

- [ ] **Step 3: Create a non-overwriting GitHub Release**

Use `GH_TOKEN: ${{ github.token }}`. Fail if `gh release view "${GITHUB_REF_NAME}"` succeeds, then run:

```bash
gh release create "${GITHUB_REF_NAME}" release-assets/* \
  --title "${GITHUB_REF_NAME}" \
  --verify-tag
```

Do not pass `--prerelease`, `--clobber`, or any option that replaces an existing release.

---

### Task 4: Validate And Commit The Workflow

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `tests/ci_ctest_checks.py`

**Interfaces:**
- Consumes: all contracts above.
- Produces: a locally validated workflow ready for authoritative native GitHub-hosted runs.

- [ ] **Step 1: Run focused tests and verify GREEN**

Run:

```bash
python3 tests/ci_ctest_checks.py
python3 tests/deploy_packaging_checks.py
```

Expected: both pass.

- [ ] **Step 2: Validate YAML/static Actions syntax locally**

Run `actionlint .github/workflows/ci.yml` if `actionlint` is installed. Otherwise run `ruby -e 'require "yaml"; YAML.parse_file(".github/workflows/ci.yml"); puts "workflow YAML parsed"'`; GitHub-hosted execution remains the authoritative Linux/Windows integration check.

- [ ] **Step 3: Run the registered focused CTest contract**

Run:

```bash
ctest --test-dir build -R '^(ci_ctest_checks|deploy_packaging_checks)$' --output-on-failure
```

Expected: 2/2 pass.

- [ ] **Step 4: Review permissions, event boundaries, and diff**

Confirm package steps are excluded from pull requests, release depends on the full matrix, only release has `contents: write`, diagnostics are matrix-specific, and tag builds use both official-build flags before staging only the two implementation files.

- [ ] **Step 5: Commit the workflow implementation**

```bash
git add .github/workflows/ci.yml tests/ci_ctest_checks.py
git commit -m "[功能修改] 完善三平台编译发布流程"
```
