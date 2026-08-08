# vcpkg Dependency Version Pinning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make local and CI vcpkg manifest installs reproducibly resolve `spdlog`, `ffmpeg`, and `pkgconf` to the approved exact versions.

**Architecture:** Keep `vcpkg.json` as the canonical dependency declaration, add one builtin registry baseline plus exact top-level overrides, and make GitHub Actions checkout the same vcpkg commit. Extend the existing source-level CI regression check so baseline, overrides, features, and workflow ref cannot drift independently; update build documentation to expose the locked versions and coordinated upgrade procedure.

**Tech Stack:** vcpkg manifest mode, JSON, GitHub Actions YAML, Python 3 source-level regression checks, CMake/CTest, Markdown.

## Global Constraints

- Pin `spdlog` to `1.17.0#0`, preserving the `fmt` feature.
- Pin `ffmpeg` to `8.0.1#2`, preserving `avcodec`, `avfilter`, `avformat`, `swresample`, and `swscale` features.
- Pin `pkgconf` to `2.5.1#4`.
- Use builtin baseline `ea1a7396b05637a53bf23c078647ecc0edee4b80` in both the manifest and CI vcpkg checkout.
- Keep C++17, Qt 6.8, platform triplets, public APIs, plugin ABI, packaging behavior, and runtime behavior unchanged.
- Do not edit generated output under `build/` or existing user changes in playback/seek files.

---

## File Map

- `tests/ci_ctest_checks.py`: owns source-level invariants for manifest version pins and the matching CI checkout ref.
- `vcpkg.json`: remains the canonical direct dependency, feature, baseline, and exact override declaration.
- `.github/workflows/ci.yml`: pins the vcpkg repository checkout used to bootstrap and resolve the manifest in CI.
- `BUILD.md`: documents the required vcpkg checkout, exact direct dependency versions, and coordinated upgrade procedure.
- `README.md`: summarizes the locked dependency versions in the quick-build path and points detailed upgrades to `BUILD.md`.

### Task 1: Lock the manifest and CI vcpkg snapshot

**Files:**
- Modify: `tests/ci_ctest_checks.py:14-24,164-176`
- Modify: `vcpkg.json:1-22`
- Modify: `.github/workflows/ci.yml:32-37`

**Interfaces:**
- Consumes: `ROOT`, `read(path)`, and `require(condition, message)` from `tests/ci_ctest_checks.py`.
- Produces: one canonical baseline string in `vcpkg.json`, exact override values, unchanged dependency feature lists, and a CI `ref` that equals the manifest baseline.

- [ ] **Step 1: Add a failing manifest and workflow regression check**

Add the following block in `main()` after the existing path declarations and before the CMake assertions:

```python
    expected_vcpkg_baseline = "ea1a7396b05637a53bf23c078647ecc0edee4b80"
    expected_vcpkg_overrides = {
        "spdlog": "1.17.0",
        "ffmpeg": "8.0.1#2",
        "pkgconf": "2.5.1#4",
    }

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
```

After `workflow` is read, add:

```python
    vcpkg_checkout_start = workflow.index("- name: Checkout vcpkg")
    install_qt_start = workflow.index("- name: Install Qt", vcpkg_checkout_start)
    vcpkg_checkout = workflow[vcpkg_checkout_start:install_qt_start]
    require(f"ref: {expected_vcpkg_baseline}" in vcpkg_checkout,
            "CI should checkout the same vcpkg commit used as builtin-baseline")
```

- [ ] **Step 2: Run the regression check and confirm the intended failure**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: FAIL with `vcpkg manifest should pin the approved builtin baseline`.

- [ ] **Step 3: Add the baseline and exact overrides to the manifest**

Replace `vcpkg.json` with the following content. vcpkg represents port revision zero by omitting `#0`, so `spdlog` uses `1.17.0`; nonzero port revisions stay in the version string.

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg/master/scripts/vcpkg.schema.json",
  "name": "pluginbased",
  "version": "1.0.0",
  "builtin-baseline": "ea1a7396b05637a53bf23c078647ecc0edee4b80",
  "dependencies": [
    {
      "name": "spdlog",
      "features": ["fmt"]
    },
    "pkgconf",
    {
      "name": "ffmpeg",
      "features": [
        "avcodec",
        "avfilter",
        "avformat",
        "swresample",
        "swscale"
      ]
    }
  ],
  "overrides": [
    {
      "name": "spdlog",
      "version": "1.17.0"
    },
    {
      "name": "ffmpeg",
      "version": "8.0.1#2"
    },
    {
      "name": "pkgconf",
      "version": "2.5.1#4"
    }
  ]
}
```

- [ ] **Step 4: Pin the CI checkout to the manifest baseline**

Change the vcpkg checkout block in `.github/workflows/ci.yml` to:

```yaml
      - name: Checkout vcpkg
        uses: actions/checkout@v4
        with:
          repository: microsoft/vcpkg
          path: vcpkg
          ref: ea1a7396b05637a53bf23c078647ecc0edee4b80
```

- [ ] **Step 5: Run focused checks**

Run:

```bash
python3 -c 'import json; json.load(open("vcpkg.json", encoding="utf-8"))'
python3 tests/ci_ctest_checks.py
git diff --check -- vcpkg.json .github/workflows/ci.yml tests/ci_ctest_checks.py
```

Expected: all commands exit 0 with no output.

- [ ] **Step 6: Commit the reproducibility contract**

```bash
git add vcpkg.json .github/workflows/ci.yml tests/ci_ctest_checks.py
git commit -m "[功能修改] 锁定 vcpkg 依赖版本"
```

### Task 2: Document locked versions and coordinated upgrades

**Files:**
- Modify: `BUILD.md:3-27`
- Modify: `BUILD.md:69-76`
- Modify: `README.md:61-84`

**Interfaces:**
- Consumes: the exact baseline and override values established by Task 1.
- Produces: copy-pasteable setup commands and one documented upgrade procedure that keeps manifest and CI pins synchronized.

- [ ] **Step 1: Update the build prerequisites and vcpkg setup**

In the `BUILD.md` environment table:

- Change the `Qt` row to minimum version `6.8` and state that Qt comes from Qt Installer or a system package manager, not from this vcpkg manifest.
- Change the `spdlog` row to exact version `1.17.0#0`.
- Change the `vcpkg` row to `ea1a7396` and explain that it must match `builtin-baseline`.

Replace the vcpkg installation commands with:

```bash
git clone https://github.com/microsoft/vcpkg
git -C vcpkg checkout ea1a7396b05637a53bf23c078647ecc0edee4b80
./vcpkg/bootstrap-vcpkg.sh   # Windows: bootstrap-vcpkg.bat
```

Immediately after the code block, add this exact-version summary:

```markdown
项目通过 `vcpkg.json` 精确锁定直接依赖：`spdlog 1.17.0#0`、
`ffmpeg 8.0.1#2`、`pkgconf 2.5.1#4`。`builtin-baseline` 同时固定传递依赖的
版本解析快照。
```

- [ ] **Step 2: Update the dependency table and upgrade procedure**

In the `BUILD.md` dependency table, use these rows:

```markdown
| spdlog | 1.17.0#0 | 结构化日志 |
| fmt | 由固定 baseline 解析 | spdlog 的格式化依赖 |
| FFmpeg | 8.0.1#2 | Media SDK demux/decode、重采样、像素转换 |
| pkgconf/pkg-config | 2.5.1#4 | CMake 查找 FFmpeg pkg-config 模块 |
```

After the table, add:

```markdown
升级 vcpkg 依赖时，必须在同一个变更中同步更新 `vcpkg.json` 的
`builtin-baseline`/`overrides` 与 `.github/workflows/ci.yml` 的 vcpkg checkout
`ref`，然后重新执行配置、构建和 CTest。不要只更新其中一处。
```

- [ ] **Step 3: Update the README quick-build summary**

Change the README dependency-table `Qt` row from `6.5+` to `6.8+`, keeping Qt outside the vcpkg manifest dependency list.

Before `## 快速构建`, add a short dependency-lock paragraph containing all four canonical values:

```markdown
vcpkg manifest 当前固定在 baseline
`ea1a7396b05637a53bf23c078647ecc0edee4b80`，直接依赖锁定为
`spdlog 1.17.0#0`、`ffmpeg 8.0.1#2` 和 `pkgconf 2.5.1#4`。本地 vcpkg checkout
应使用同一提交；升级流程见 [BUILD.md](BUILD.md)。
```

- [ ] **Step 4: Verify documentation consistency**

Run:

```bash
rg -n 'ea1a7396b05637a53bf23c078647ecc0edee4b80|spdlog 1\.17\.0#0|ffmpeg 8\.0\.1#2|pkgconf 2\.5\.1#4' vcpkg.json .github/workflows/ci.yml BUILD.md README.md
git diff --check -- BUILD.md README.md
```

Expected: each canonical version appears in the intended manifest/documentation locations and `git diff --check` exits 0.

- [ ] **Step 5: Commit the documentation**

```bash
git add BUILD.md README.md
git commit -m "[功能修改] 记录 vcpkg 固定依赖版本"
```

## Integration Verification

- [ ] Run the source-level contract check:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: exit 0 with no output.

- [ ] Run the registered CTest check through the existing configured build:

```bash
ctest --test-dir build -R '^ci_ctest_checks$' --output-on-failure
```

Expected: `ci_ctest_checks` passes. This command does not reconfigure or edit generated build output.

- [ ] Inspect the final scoped diff and repository state:

```bash
git diff HEAD~2 -- vcpkg.json .github/workflows/ci.yml tests/ci_ctest_checks.py BUILD.md README.md
git status --short
```

Expected: the scoped diff contains only the approved pinning, CI, regression-check, and documentation changes; pre-existing playback/seek changes remain untouched.

- [ ] When a git checkout of the fixed vcpkg commit and dependency network access are available, run the end-to-end manifest/configure verification:

```bash
cmake -S . -B build-vcpkg-pin-check \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/private/tmp/pluginbased-vcpkg-ea1a739/scripts/buildsystems/vcpkg.cmake
cmake --build build-vcpkg-pin-check --parallel
ctest --test-dir build-vcpkg-pin-check --output-on-failure
```

Expected: vcpkg's install plan reports `spdlog@1.17.0`, `ffmpeg@8.0.1#2`, and `pkgconf@2.5.1#4`; configure, build, and CTest pass. Create `/private/tmp/pluginbased-vcpkg-ea1a739` by cloning `microsoft/vcpkg` and checking out `ea1a7396b05637a53bf23c078647ecc0edee4b80` before running these commands.

## Completion Definition

- `vcpkg.json` contains the approved baseline and exact direct dependency overrides without changing features.
- CI checks out the same vcpkg commit.
- The existing `ci_ctest_checks` test rejects baseline, override, feature, or CI-ref drift.
- README and build documentation show the exact versions and coordinated upgrade rule.
- Focused Python and CTest checks pass; any unavailable full dependency install/build path is reported explicitly.
