# Theme Switch Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add first-phase skin switching by reusing `QtQuickComponents` `ComponentTheme` Dark/Light styles, with persistence and host QML cleanup.

**Architecture:** `ComponentTheme` remains the only theme token source. `PluginBased` owns user preference persistence and UI entry points, while app and plugin QML bind to `ComponentTheme` tokens. This phase does not add JSON theme files or file watcher hot reload.

**Tech Stack:** Qt 6, QML, C++17, `QSettings`, `QuickUI.Components 1.0`, CTest Python structure checks.

---

## File Structure

- Modify `app/AppConfig.h` and `app/AppConfig.cpp`: add `[ui] theme=dark|light` persistence.
- Modify `app/AppController.h` and `app/AppController.cpp`: expose theme state and QML invokables, apply persisted theme on startup and reload.
- Modify `app/qml/main.qml`: add a toolbar switch action and replace hard-coded shell colors with `ComponentTheme`.
- Modify `app/qml/HomePanel.qml`: import `QuickUI.Components 1.0` and replace host-page hard-coded colors with `ComponentTheme` tokens.
- Modify `tests/ci_ctest_checks.py` or add a focused test script if checks become too broad: assert theme persistence/API/QML token usage exists.
- Modify `README.md`: document first-phase skin switching and plugin guidance.

## Task 1: Persist Theme Selection

**Files:**
- Modify: `app/AppConfig.h`
- Modify: `app/AppConfig.cpp`
- Test: `tests/ci_ctest_checks.py`

- [ ] **Step 1: Write the failing structure check**

Add checks requiring UI theme config support:

```python
app_config_h = read("app/AppConfig.h")
app_config_cpp = read("app/AppConfig.cpp")
require("themeName() const" in app_config_h,
        "AppConfig should expose the persisted UI theme")
require("setThemeName" in app_config_h,
        "AppConfig should allow updating the persisted UI theme")
require('beginGroup(QStringLiteral("ui"))' in app_config_cpp,
        "AppConfig should persist UI settings in [ui]")
require('QStringLiteral("theme")' in app_config_cpp,
        "AppConfig should read and write the ui/theme key")
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: fails because `themeName()` is not present.

- [ ] **Step 3: Add minimal config API**

In `app/AppConfig.h`, add:

```cpp
QString themeName() const { return m_themeName; }
void setThemeName(const QString& themeName);
```

Add private member:

```cpp
QString m_themeName { "dark" };
```

In `app/AppConfig.cpp`, extend `applyDefaults()` with `[ui]`:

```cpp
m_settings->beginGroup(QStringLiteral("ui"));
if (!m_settings->contains(QStringLiteral("theme")))
    m_settings->setValue(QStringLiteral("theme"), QStringLiteral("dark"));
m_settings->endGroup();
```

Extend `load()` after `[log]` with:

```cpp
m_settings->beginGroup(QStringLiteral("ui"));
m_themeName = m_settings->value(QStringLiteral("theme"), QStringLiteral("dark")).toString().trimmed().toLower();
if (m_themeName != QStringLiteral("dark") && m_themeName != QStringLiteral("light"))
    m_themeName = QStringLiteral("dark");
m_settings->endGroup();
```

Add:

```cpp
void AppConfig::setThemeName(const QString& themeName)
{
    const QString normalized = themeName.trimmed().toLower();
    if (normalized == QStringLiteral("light"))
        m_themeName = QStringLiteral("light");
    else
        m_themeName = QStringLiteral("dark");
}
```

Extend `save()` with:

```cpp
m_settings->beginGroup(QStringLiteral("ui"));
m_settings->setValue(QStringLiteral("theme"), m_themeName);
m_settings->endGroup();
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: passes.

## Task 2: Expose Theme Control Through AppController

**Files:**
- Modify: `app/AppController.h`
- Modify: `app/AppController.cpp`
- Test: `tests/ci_ctest_checks.py`

- [ ] **Step 1: Write the failing structure check**

Add:

```python
app_controller_h = read("app/AppController.h")
app_controller_cpp = read("app/AppController.cpp")
require("currentTheme READ currentTheme" in app_controller_h,
        "AppController should expose currentTheme to QML")
require("setTheme(" in app_controller_h,
        "AppController should expose setTheme to QML")
require("toggleTheme" in app_controller_h,
        "AppController should expose toggleTheme to QML")
require("ComponentTheme::instance().setStyle" in app_controller_cpp,
        "AppController should apply theme changes through ComponentTheme")
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: fails because `currentTheme` is not exposed.

- [ ] **Step 3: Add controller API**

In `app/AppController.h`, add:

```cpp
Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY currentThemeChanged)
QString currentTheme() const { return m_currentTheme; }
Q_INVOKABLE void setTheme(const QString& themeName);
Q_INVOKABLE void toggleTheme();
```

Add signal and member:

```cpp
void currentThemeChanged();
QString m_currentTheme { "dark" };
```

In `app/AppController.cpp`, include the component theme header:

```cpp
#include "ComponentTheme.h"
```

Add helper-level behavior through methods:

```cpp
void AppController::setTheme(const QString& themeName)
{
    const QString normalized = themeName.trimmed().toLower();
    const QString next = normalized == QStringLiteral("light")
        ? QStringLiteral("light")
        : QStringLiteral("dark");

    ComponentTheme::instance().setStyle(next == QStringLiteral("light")
        ? ComponentTheme::Light
        : ComponentTheme::Dark);

    AppConfig::instance().setThemeName(next);
    AppConfig::instance().save();

    if (m_currentTheme == next)
        return;
    m_currentTheme = next;
    emit currentThemeChanged();
}

void AppController::toggleTheme()
{
    setTheme(m_currentTheme == QStringLiteral("dark")
        ? QStringLiteral("light")
        : QStringLiteral("dark"));
}
```

Apply persisted theme during startup or config reload:

```cpp
setTheme(AppConfig::instance().themeName());
```

- [ ] **Step 4: Build to catch include/link issues**

Run:

```bash
cmake --build build --parallel
```

Expected: `PluginBasedApp` builds successfully.

## Task 3: Add QML Switch Entry

**Files:**
- Modify: `app/qml/main.qml`
- Test: `tests/ci_ctest_checks.py`

- [ ] **Step 1: Write the failing QML check**

Add:

```python
main_qml = read("app/qml/main.qml")
require("AppController.toggleTheme()" in main_qml,
        "main toolbar should expose a theme toggle action")
require("AppController.currentTheme" in main_qml,
        "main toolbar should bind theme toggle state to AppController.currentTheme")
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: fails because the toolbar has no theme action.

- [ ] **Step 3: Add toolbar toggle**

In `app/qml/main.qml`, before the quit button, add an `IconButton`:

```qml
IconButton {
    iconText: AppController.currentTheme === "dark" ? "☀" : "☾"
    tooltip:  AppController.currentTheme === "dark" ? "切换浅色皮肤" : "切换深色皮肤"
    onClicked: AppController.toggleTheme()
}
```

- [ ] **Step 4: Run test and build**

Run:

```bash
python3 tests/ci_ctest_checks.py
cmake --build build --parallel
```

Expected: both pass.

## Task 4: Replace Host Hard-Coded Colors With ComponentTheme

**Files:**
- Modify: `app/qml/main.qml`
- Modify: `app/qml/HomePanel.qml`
- Test: `tests/ci_ctest_checks.py`

- [ ] **Step 1: Write the failing token usage check**

Add:

```python
home_panel_qml = read("app/qml/HomePanel.qml")
main_qml = read("app/qml/main.qml")
require("ComponentTheme.surface" in main_qml,
        "main.qml should use ComponentTheme surface tokens")
require("ComponentTheme.textPrimary" in main_qml,
        "main.qml should use ComponentTheme text tokens")
require("import QuickUI.Components 1.0" in home_panel_qml,
        "HomePanel should import QuickUI.Components")
require("ComponentTheme.surface" in home_panel_qml,
        "HomePanel should use ComponentTheme surface tokens")
require("ComponentTheme.textPrimary" in home_panel_qml,
        "HomePanel should use ComponentTheme text tokens")
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 tests/ci_ctest_checks.py
```

Expected: fails because `HomePanel.qml` does not import `QuickUI.Components`.

- [ ] **Step 3: Update main shell colors**

Replace representative shell colors in `app/qml/main.qml`:

```qml
color: ComponentTheme.surface
```

Toolbar:

```qml
color: ComponentTheme.surface
border.color: ComponentTheme.separator
```

Text:

```qml
color: ComponentTheme.textPrimary
```

Animations should use:

```qml
duration: ComponentTheme.durationFast
```

- [ ] **Step 4: Update HomePanel colors**

Add:

```qml
import QuickUI.Components 1.0
```

Replace panel/card colors with available tokens:

```qml
color: ComponentTheme.surface
border.color: ComponentTheme.separator
```

Text:

```qml
color: ComponentTheme.textPrimary
color: ComponentTheme.textSecondary
color: ComponentTheme.textDisabled
```

Accent:

```qml
color: ComponentTheme.accent
```

- [ ] **Step 5: Run tests and build**

Run:

```bash
python3 tests/ci_ctest_checks.py
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all pass.

## Task 5: Document Phase 1 Scope

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add README section**

Document:

```markdown
### 皮肤切换

宿主和插件统一使用 `QuickUI.Components 1.0` 的 `ComponentTheme`。第一阶段支持内置 `Dark` / `Light` 运行时切换，并把当前选择保存到 `pluginbased.ini` 的 `[ui] theme`。插件 QML 应避免写死宿主级颜色，优先使用 `ComponentTheme.surface`、`ComponentTheme.textPrimary`、`ComponentTheme.accent` 等 token。
```

- [ ] **Step 2: Run final verification**

Run:

```bash
git diff --check
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected: all pass.

## Self-Review

- Spec coverage: phase 1 covers built-in Dark/Light switching, persistence, host QML token adoption, and plugin guidance.
- Scope control: JSON theme packs and `QFileSystemWatcher` hot reload are excluded from this phase.
- Type consistency: the plan uses `themeName`, `currentTheme`, `setTheme`, and `toggleTheme` consistently across config, controller, and QML.
