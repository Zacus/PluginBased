# Host Plugin Generator Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple host plugin path discovery and plugin generator scaffolding without changing QML APIs or generated plugin behavior.

**Architecture:** Add small non-QObject helpers for plugin path resolution, template rendering, and scaffold writing. Keep `AppController` and `PluginTemplateGenerator` as public-facing facades that delegate implementation details to helpers.

**Tech Stack:** C++17, Qt 6 Core/QML, CMake, Python architecture checks, CTest.

---

## File Map

- Create `app/PluginPathResolver.h/.cpp`: app-layer helper for runtime plugin directory selection.
- Modify `app/AppController.cpp`: delegate plugin directory selection to `PluginPathResolver`.
- Modify `app/CMakeLists.txt`: compile `PluginPathResolver`.
- Create `tools/plugin_generator/PluginGeneratorOptions.h`: shared value type for parsed generator options.
- Create `tools/plugin_generator/PluginTemplateRenderer.h/.cpp`: pure template text rendering.
- Create `tools/plugin_generator/PluginScaffoldWriter.h/.cpp`: filesystem writes and icon copy.
- Modify `tools/plugin_generator/PluginTemplateGenerator.h/.cpp`: keep QML-facing orchestration, delegate rendering/writing.
- Modify `tools/plugin_generator/CMakeLists.txt`: compile new generator helpers into app and smoke test.
- Create `tests/host_plugin_generator_decoupling_checks.py`: architecture guard for host/generator boundaries.
- Modify `CMakeLists.txt`: register the architecture guard in CTest.

## Task 1: Add Architecture Guard

**Files:**
- Create: `tests/host_plugin_generator_decoupling_checks.py`
- Modify: `CMakeLists.txt`

- [x] **Step 1: Write failing architecture checks**

Create a Python check requiring:

- `PluginPathResolver.h/.cpp` exist and include purpose comments.
- `AppController.cpp` uses `PluginPathResolver::resolve`.
- `AppController.cpp` no longer declares the plugin path `candidates` list inline.
- `PluginGeneratorOptions.h` exists.
- `PluginTemplateRenderer.h/.cpp` exist and own `headerText`, `sourceText`, `metadataText`, `cmakeText`, `qmlText`, and `translationText`.
- `PluginScaffoldWriter.h/.cpp` exist and own plugin directory creation and icon copying.
- `PluginTemplateGenerator.h` no longer exposes template rendering methods.
- CMake compiles all new files.

- [x] **Step 2: Run check and verify RED**

Run:

```bash
python3 tests/host_plugin_generator_decoupling_checks.py
```

Expected: FAIL with `PluginPathResolver.h should exist`.

## Task 2: Extract PluginPathResolver

**Files:**
- Create: `app/PluginPathResolver.h`
- Create: `app/PluginPathResolver.cpp`
- Modify: `app/AppController.cpp`
- Modify: `app/CMakeLists.txt`

- [x] **Step 1: Implement resolver**

Add `PluginPathResolution` and `PluginPathResolver::resolve(const QString& appDir)` with the existing candidate order:

1. `<appDir>/plugins`
2. `<appDir>/../PlugIns`
3. `<appDir>/../plugins`
4. `<appDir>/../../../../plugins`

Use `*.dll` on Windows and `*.so` elsewhere, matching current behavior.

- [x] **Step 2: Delegate from AppController**

Replace inline candidate/filter scanning in `AppController::initPlugins()` with `PluginPathResolver::resolve(QCoreApplication::applicationDirPath())`.

- [x] **Step 3: Run architecture check**

Run:

```bash
python3 tests/host_plugin_generator_decoupling_checks.py
```

Expected: still FAIL until generator helpers are added.

## Task 3: Extract Plugin Generator Options And Renderer

**Files:**
- Create: `tools/plugin_generator/PluginGeneratorOptions.h`
- Create: `tools/plugin_generator/PluginTemplateRenderer.h`
- Create: `tools/plugin_generator/PluginTemplateRenderer.cpp`
- Modify: `tools/plugin_generator/PluginTemplateGenerator.h`
- Modify: `tools/plugin_generator/PluginTemplateGenerator.cpp`
- Modify: `tools/plugin_generator/CMakeLists.txt`

- [x] **Step 1: Add shared options type**

Move the generator options struct into `PluginGeneratorOptions.h`.

- [x] **Step 2: Move template rendering**

Move `headerText`, `sourceText`, `metadataText`, `cmakeText`, `qmlText`, `translationText`, and escaping helpers into `PluginTemplateRenderer`.

- [x] **Step 3: Delegate rendering from generator**

Keep `PluginTemplateGenerator::parseOptions()` and `generate()` as the public orchestration path, but call `PluginTemplateRenderer` for generated text.

## Task 4: Extract PluginScaffoldWriter

**Files:**
- Create: `tools/plugin_generator/PluginScaffoldWriter.h`
- Create: `tools/plugin_generator/PluginScaffoldWriter.cpp`
- Modify: `tools/plugin_generator/PluginTemplateGenerator.h`
- Modify: `tools/plugin_generator/PluginTemplateGenerator.cpp`
- Modify: `tools/plugin_generator/CMakeLists.txt`

- [x] **Step 1: Move filesystem writes**

Move output directory creation, plugin directory guard, optional subdirectory creation, text file writing, and icon copy into `PluginScaffoldWriter`.

- [x] **Step 2: Keep result contract**

Preserve current QVariantMap fields:

- success: `ok=true`, `path`, `message="插件已生成"`
- failure: `ok=false`, `message`

- [x] **Step 3: Run generator smoke test**

Run:

```bash
cmake --build build --target PluginGeneratorBackendSmokeTest --parallel
./build/tools/plugin_generator/PluginGeneratorBackendSmokeTest
```

Expected: exit 0.

## Task 5: Verify And Commit

**Files:**
- All files touched above.

- [x] **Step 1: Run local architecture and regression checks**

Run:

```bash
python3 tests/host_plugin_generator_decoupling_checks.py
python3 tests/plugin_generator_checks.py
```

- [x] **Step 2: Build and run CTest**

Run:

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- [x] **Step 3: Commit**

Commit message:

```bash
git commit -m "[功能修改] 解耦宿主插件路径和生成器模板"
```

Completed in commit `98abb1d`.

## Follow-up: Comment Contract Sync

- [x] Keep the newly added helper file purpose comments in Chinese, matching the repository's current documentation style.
- [x] Update `tests/host_plugin_generator_decoupling_checks.py` so the architecture guard validates those Chinese purpose comments instead of stale English text.
