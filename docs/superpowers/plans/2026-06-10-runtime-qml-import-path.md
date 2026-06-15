# Runtime QML Import Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make packaged builds resolve QML modules from runtime `qml/` directories while keeping the current build-directory fallback for development builds.

**Architecture:** Keep the change local to startup wiring in `app/main.cpp`. Add a small helper that derives candidate import paths from `QCoreApplication::applicationDirPath()`, adds existing directories to `QQmlApplicationEngine`, and logs skipped or added paths. Extend the existing Python structure check so future changes cannot regress to build-directory-only imports.

**Tech Stack:** C++17, Qt 6 `QQmlApplicationEngine`, `QDir`, existing spdlog wrapper macros, Python structure checks through CTest.

---

### Task 1: Add Failing Structure Check

**Files:**
- Modify: `tests/ci_ctest_checks.py`
- Test: `tests/ci_ctest_checks.py`

- [x] **Step 1: Write the failing test**

Add checks that `app/main.cpp` contains runtime QML path handling:

```python
    main_cpp = read("app/main.cpp")
    require('"/qml"' in main_cpp,
            "main.cpp should add appDir/qml as a runtime QML import path")
    require('"../Resources/qml"' in main_cpp,
            "main.cpp should add macOS bundle Resources/qml as a runtime QML import path")
    require("QML_IMPORT_PATH" in main_cpp,
            "main.cpp should keep QML_IMPORT_PATH as a development fallback")
    require("addRuntimeQmlImportPaths" in main_cpp,
            "main.cpp should centralize runtime QML import path setup")
    require(main_cpp.find("addRuntimeQmlImportPaths") < main_cpp.find("engine.load(entryUrl)"),
            "main.cpp should add QML import paths before loading the root QML entry")
```

- [x] **Step 2: Run test to verify it fails**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: FAIL with `main.cpp should add appDir/qml as a runtime QML import path`.

### Task 2: Implement Runtime QML Import Paths

**Files:**
- Modify: `app/main.cpp`
- Test: `tests/ci_ctest_checks.py`

- [x] **Step 1: Write minimal implementation**

Add a helper near the top of `app/main.cpp`:

```cpp
namespace {

QStringList qmlImportPathCandidates(const QString& appDir)
{
    QStringList candidates;
    candidates << QDir::cleanPath(appDir + QStringLiteral("/qml"));
    candidates << QDir::cleanPath(appDir + QStringLiteral("/../Resources/qml"));
#ifdef QML_IMPORT_PATH
    candidates << QDir::cleanPath(QStringLiteral(QML_IMPORT_PATH));
#endif
    candidates.removeDuplicates();
    return candidates;
}

void addRuntimeQmlImportPaths(QQmlApplicationEngine& engine, const QString& appDir)
{
    int addedCount = 0;
    for (const QString& path : qmlImportPathCandidates(appDir)) {
        if (!QDir(path).exists()) {
            LOG_DEBUG("QML import path missing, skipped: {}", path.toStdString());
            continue;
        }

        engine.addImportPath(path);
        ++addedCount;
        LOG_INFO("QML import path added: {}", path.toStdString());
    }

    if (addedCount == 0) {
        LOG_WARN("No runtime QML import paths found; QML module imports may fail");
    }
}

} // namespace
```

Then replace the direct `engine.addImportPath(QStringLiteral(QML_IMPORT_PATH));` block with:

```cpp
    addRuntimeQmlImportPaths(engine, QCoreApplication::applicationDirPath());
```

- [x] **Step 2: Run structure test to verify it passes**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: PASS with exit code 0.

### Task 3: Verify Build and CTest

**Files:**
- Verify: project build and registered tests

- [x] **Step 1: Build**

Run:

```bash
cmake --build build --parallel
```

Expected: build exits 0.

- [x] **Step 2: Run CTest**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: all registered tests pass.
