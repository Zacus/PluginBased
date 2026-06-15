# Theme Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 解决主题系统设计审核中暴露的职责过重、宿主/组件库路径契约不清、默认主题重复定义、控制器耦合和测试脆弱问题。

**Architecture:** QtQuickComponents 保留主题 token 和 QML 单例能力，但把 JSON 解析、文件监听、默认主题资源加载拆成独立小模块。PluginBased 新增宿主侧 `AppThemeService`，统一负责运行时主题目录、配置回退和 `ComponentTheme` 接入，`AppController` 只做 QML 门面。主题资源契约统一为“宿主运行时优先 `<app>/themes`，组件库提供内置 fallback”。

**Tech Stack:** Qt 6, QML singleton, C++17, CMake, CTest, QtTest, QFileSystemWatcher, QJsonDocument.

---

## 背景问题

设计审核识别出 5 个需要处理的问题：

1. `ComponentTheme` 同时负责状态、JSON 解析、字段校验、文件扫描、热加载和内置主题，职责过重。
2. QtQuickComponents 安装主题到 `share/QtQuickComponents/themes`，PluginBased 运行时扫描 `<app>/themes` 一类目录，资源契约不统一。
3. `AppController` 混入主题路径解析、热加载初始化、配置保存、失败回退，控制器职责变宽。
4. `applyDark()` / `applyLight()` 与 `themes/dark.json` / `themes/light.json` 存在重复 token 定义。
5. PluginBased 的 `ci_ctest_checks.py` 对实现细节做字符串断言，后续重构容易误报。

## 目标边界

必须保留：

- QML 使用方式：`ComponentTheme.accent`、`ComponentTheme.loadTheme("dark")`、`ComponentTheme.hotReloadEnabled = true`。
- PluginBased 配置方式：`pluginbased.ini` 的 `[ui] theme=<id>`。
- 热加载行为：修改当前 JSON 后，绑定 `ComponentTheme` token 的 UI 自动刷新。
- 当前 CTest 全部继续通过。

不在本轮做：

- 主题编辑器 UI。
- XML 主题格式。
- 主题继承、变量引用、schema migration。
- 多用户配置目录。

## 文件结构

QtQuickComponents 仓库：`/Users/zs/Downloads/QtQuickComponents`

- Create: `src/theme/ThemeTokens.h`
  - 纯数据结构，承载所有颜色、尺寸、字体、动效 token。
- Create: `src/theme/ThemeJsonLoader.h`
  - 声明 JSON 加载、校验和内置主题加载 API。
- Create: `src/theme/ThemeJsonLoader.cpp`
  - 实现 JSON 解析和字段校验，不访问 QML，不发信号。
- Create: `src/theme/ThemeFileWatcher.h`
  - 声明热加载 watcher API。
- Create: `src/theme/ThemeFileWatcher.cpp`
  - 封装 `QFileSystemWatcher`、debounce timer、poll fallback。
- Modify: `src/theme/ComponentTheme.h`
  - 移除 `ThemeTokens` 内部结构、watcher 成员、parse helper 声明。
- Modify: `src/theme/ComponentTheme.cpp`
  - 只保留当前 token 状态、QML API、`applyTokens()`、内置 fallback 调用。
- Modify: `CMakeLists.txt`
  - 增加新 `.h/.cpp`，把 `themes/*.json` 同时作为运行时复制文件和 Qt resource。
- Modify: `tests/tst_component_theme.cpp`
  - 保留 ComponentTheme 行为测试，减少 JSON 解析内部细节。
- Create: `tests/tst_theme_json_loader.cpp`
  - 专门测试 JSON 解析和校验。
- Create: `tests/tst_theme_file_watcher.cpp`
  - 专门测试热加载 watcher。
- Modify: `tests/CMakeLists.txt`
  - 增加新测试目标。

PluginBased 仓库：`/Users/zs/Downloads/PluginBased`

- Create: `app/AppThemeService.h`
  - 声明宿主侧主题服务。
- Create: `app/AppThemeService.cpp`
  - 实现主题目录解析、热加载开关、配置保存和 fallback。
- Modify: `app/AppController.h`
  - 保留 QML 属性和 slots，不暴露主题实现细节。
- Modify: `app/AppController.cpp`
  - 移除匿名 namespace 主题 helper，委托 `AppThemeService`。
- Modify: `app/CMakeLists.txt`
  - 增加 `AppThemeService.h/.cpp`。
- Modify: `CMakeLists.txt`
  - 明确复制默认主题到 `${CMAKE_BINARY_DIR}/themes`，安装到运行时主题目录。
- Modify: `tests/ci_ctest_checks.py`
  - 去掉对 `setThemeDirectory`、`setHotReloadEnabled(true)` 等实现字符串的断言，替换成架构级检查。

---

### Task 1: Extract ThemeTokens And ThemeJsonLoader

**Files:**

- Create: `/Users/zs/Downloads/QtQuickComponents/src/theme/ThemeTokens.h`
- Create: `/Users/zs/Downloads/QtQuickComponents/src/theme/ThemeJsonLoader.h`
- Create: `/Users/zs/Downloads/QtQuickComponents/src/theme/ThemeJsonLoader.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/src/theme/ComponentTheme.h`
- Modify: `/Users/zs/Downloads/QtQuickComponents/src/theme/ComponentTheme.cpp`
- Create: `/Users/zs/Downloads/QtQuickComponents/tests/tst_theme_json_loader.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/tests/CMakeLists.txt`
- Modify: `/Users/zs/Downloads/QtQuickComponents/CMakeLists.txt`

- [ ] **Step 1: Write failing ThemeJsonLoader tests**

Add `tests/tst_theme_json_loader.cpp` with these behaviors:

```cpp
#include <QtTest/QtTest>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "ThemeJsonLoader.h"

class ThemeJsonLoaderTest : public QObject
{
    Q_OBJECT

private slots:
    void loadsCompleteJson();
    void rejectsOversizedInteger();
    void acceptsZeroRadius();
    void rejectsInvalidColor();

private:
    static QString writeFile(const QString& directory, const QString& name, const QString& content)
    {
        const QString path = QDir(directory).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return {};
        file.write(content.toUtf8());
        return path;
    }

    static QString validThemeJson(int buttonRadius = 6, int inputRadius = 6)
    {
        return QStringLiteral(R"json({
  "id": "enterprise",
  "name": "Enterprise",
  "colors": {
    "accent": "#102030",
    "accentHover": "#223344",
    "accentPressed": "#334455",
    "accentDisabled": "#445566",
    "iconColor": "#556677",
    "iconColorPressed": "#667788",
    "buttonHover": "#778899",
    "buttonPressed": "#8899aa",
    "trackBg": "#99aabb",
    "trackBuffer": "#aabbcc",
    "handleBorder": "#bbccdd",
    "textPrimary": "#ccddee",
    "textSecondary": "#ddeeff",
    "textDisabled": "#123456",
    "textOnAccent": "#ffffff",
    "surface": "#101112",
    "surfaceHover": "#131415",
    "separator": "#161718",
    "inputBg": "#191a1b",
    "inputBorder": "#1c1d1e",
    "inputFocus": "#1f2021",
    "inputText": "#222324",
    "inputPlaceholder": "#252627"
  },
  "sizes": {
    "buttonSize": 34,
    "buttonRadius": %1,
    "inputHeight": 36,
    "inputRadius": %2,
    "trackHeight": 4,
    "handleSize": 14
  },
  "fonts": {
    "fontFamily": "",
    "fontSize": 16,
    "fontSizeLabel": 13,
    "fontSizeCaption": 11
  },
  "motion": {
    "durationFast": 80,
    "durationNormal": 120,
    "reducedMotion": false
  }
})json").arg(buttonRadius).arg(inputRadius);
    }
};

void ThemeJsonLoaderTest::loadsCompleteJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(dir.path(), QStringLiteral("enterprise.json"), validThemeJson());

    ThemeLoadResult result = ThemeJsonLoader::loadFile(path);

    QVERIFY(result.ok);
    QCOMPARE(result.tokens.id, QStringLiteral("enterprise"));
    QCOMPARE(result.tokens.name, QStringLiteral("Enterprise"));
    QCOMPARE(result.tokens.accent, QColor(QStringLiteral("#102030")));
    QCOMPARE(result.tokens.buttonRadius, 6);
    QCOMPARE(result.error, QString());
}

void ThemeJsonLoaderTest::rejectsOversizedInteger()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString json = validThemeJson();
    json.replace(QStringLiteral("\"buttonSize\": 34"), QStringLiteral("\"buttonSize\": 2147483648"));
    const QString path = writeFile(dir.path(), QStringLiteral("bad.json"), json);

    ThemeLoadResult result = ThemeJsonLoader::loadFile(path);

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("buttonSize")));
}

void ThemeJsonLoaderTest::acceptsZeroRadius()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(dir.path(), QStringLiteral("sharp.json"), validThemeJson(0, 0));

    ThemeLoadResult result = ThemeJsonLoader::loadFile(path);

    QVERIFY(result.ok);
    QCOMPARE(result.tokens.buttonRadius, 0);
    QCOMPARE(result.tokens.inputRadius, 0);
}

void ThemeJsonLoaderTest::rejectsInvalidColor()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString json = validThemeJson();
    json.replace(QStringLiteral("\"accent\": \"#102030\""), QStringLiteral("\"accent\": \"not-a-color\""));
    const QString path = writeFile(dir.path(), QStringLiteral("bad-color.json"), json);

    ThemeLoadResult result = ThemeJsonLoader::loadFile(path);

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("accent")));
}

QTEST_MAIN(ThemeJsonLoaderTest)

#include "tst_theme_json_loader.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --target tst_theme_json_loader --parallel
ctest --test-dir build --output-on-failure -R ThemeJsonLoader
```

Expected: build fails because `ThemeJsonLoader.h` does not exist.

- [ ] **Step 3: Add ThemeTokens and ThemeJsonLoader API**

Create `src/theme/ThemeTokens.h`:

```cpp
#pragma once

#include <QColor>
#include <QString>

struct ThemeTokens
{
    QString id;
    QString name;

    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentDisabled;
    QColor iconColor;
    QColor iconColorPressed;
    QColor buttonHover;
    QColor buttonPressed;
    QColor trackBg;
    QColor trackBuffer;
    QColor handleBorder;
    QColor textPrimary;
    QColor textSecondary;
    QColor textDisabled;
    QColor textOnAccent;
    QColor surface;
    QColor surfaceHover;
    QColor separator;
    QColor inputBg;
    QColor inputBorder;
    QColor inputFocus;
    QColor inputText;
    QColor inputPlaceholder;

    int buttonSize = 34;
    int buttonRadius = 6;
    int inputHeight = 36;
    int inputRadius = 6;
    int trackHeight = 4;
    int handleSize = 14;

    QString fontFamily;
    int fontSize = 16;
    int fontSizeLabel = 13;
    int fontSizeCaption = 11;

    int durationFast = 80;
    int durationNormal = 120;
    bool reducedMotion = false;
};
```

Create `src/theme/ThemeJsonLoader.h`:

```cpp
#pragma once

#include "ThemeTokens.h"

#include <QString>
#include <QStringList>

struct ThemeLoadResult
{
    bool ok = false;
    ThemeTokens tokens;
    QString error;
};

class ThemeJsonLoader
{
public:
    static ThemeLoadResult loadFile(const QString& path);
    static ThemeLoadResult loadData(const QByteArray& data, const QString& sourceName);
    static ThemeLoadResult loadBuiltInTheme(const QString& themeId);
    static bool isValidThemeId(const QString& themeId);
    static QStringList builtInThemeIds();
};
```

Move the existing JSON parsing lambdas from `ComponentTheme::parseThemeFile()` into `ThemeJsonLoader.cpp`. Keep the same validation behavior:

- required objects: `colors`, `sizes`, `fonts`, `motion`
- required colors: every current color token
- radius minimum: `0`
- other size/font minimum: `1`
- motion duration minimum: `0`
- maximum for every integer: `std::numeric_limits<int>::max()`

- [ ] **Step 4: Wire CMake and tests**

Modify `/Users/zs/Downloads/QtQuickComponents/CMakeLists.txt`:

```cmake
set(QTC_PUBLIC_HEADERS
    src/theme/ComponentTheme.h
    src/theme/ThemeJsonLoader.h
    src/theme/ThemeTokens.h
    ...
)

set(QTC_CPP_SOURCES
    src/theme/ComponentTheme.cpp
    src/theme/ThemeJsonLoader.cpp
    ...
)
```

Modify `/Users/zs/Downloads/QtQuickComponents/tests/CMakeLists.txt`:

```cmake
qt_add_executable(tst_theme_json_loader
    tst_theme_json_loader.cpp
)

target_link_libraries(tst_theme_json_loader PRIVATE
    QtQuickComponents
    Qt6::Test
)

add_test(NAME ThemeJsonLoader COMMAND tst_theme_json_loader)
```

- [ ] **Step 5: Update ComponentTheme to use ThemeJsonLoader**

In `ComponentTheme.h`, remove the private nested `ThemeTokens` struct and `parseThemeFile()` declaration. Include:

```cpp
#include "ThemeTokens.h"
```

In `ComponentTheme.cpp`:

```cpp
#include "ThemeJsonLoader.h"
```

Replace `loadThemeFile()` body:

```cpp
bool ComponentTheme::loadThemeFile(const QString& path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const ThemeLoadResult result = ThemeJsonLoader::loadFile(absolutePath);
    if (!result.ok) {
        setLastError(result.error);
        return false;
    }

    applyTokens(result.tokens, Custom, absolutePath);
    emit styleChanged();
    return true;
}
```

Replace `reloadCurrentTheme()` parsing:

```cpp
const ThemeLoadResult result = ThemeJsonLoader::loadFile(m_currentThemeFile);
if (!result.ok) {
    setLastError(result.error);
    configureThemeWatcher();
    return false;
}

applyTokens(result.tokens, Custom, m_currentThemeFile);
emit styleChanged();
return true;
```

- [ ] **Step 6: Run focused and full tests**

Run:

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "ThemeJsonLoader|ComponentTheme"
ctest --test-dir build --output-on-failure
```

Expected: all QtQuickComponents tests pass.

- [ ] **Step 7: Commit**

```bash
cd /Users/zs/Downloads/QtQuickComponents
git add CMakeLists.txt src/theme/ComponentTheme.cpp src/theme/ComponentTheme.h src/theme/ThemeJsonLoader.cpp src/theme/ThemeJsonLoader.h src/theme/ThemeTokens.h tests/CMakeLists.txt tests/tst_theme_json_loader.cpp
git commit -m "[重构] 拆分主题 JSON 加载逻辑"
```

---

### Task 2: Extract ThemeFileWatcher

**Files:**

- Create: `/Users/zs/Downloads/QtQuickComponents/src/theme/ThemeFileWatcher.h`
- Create: `/Users/zs/Downloads/QtQuickComponents/src/theme/ThemeFileWatcher.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/src/theme/ComponentTheme.h`
- Modify: `/Users/zs/Downloads/QtQuickComponents/src/theme/ComponentTheme.cpp`
- Create: `/Users/zs/Downloads/QtQuickComponents/tests/tst_theme_file_watcher.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/tests/CMakeLists.txt`
- Modify: `/Users/zs/Downloads/QtQuickComponents/CMakeLists.txt`

- [ ] **Step 1: Write failing watcher test**

Create `tests/tst_theme_file_watcher.cpp`:

```cpp
#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ThemeFileWatcher.h"

class ThemeFileWatcherTest : public QObject
{
    Q_OBJECT

private slots:
    void emitsReloadRequestedForFileRewrite();
    void emitsReloadRequestedForAtomicReplace();

private:
    static QString writeFile(const QString& directory, const QString& fileName, const QString& text)
    {
        const QString path = QDir(directory).filePath(fileName);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return {};
        file.write(text.toUtf8());
        return path;
    }

    static QString atomicReplace(const QString& directory, const QString& fileName, const QString& text)
    {
        const QString path = QDir(directory).filePath(fileName);
        const QString tempPath = QDir(directory).filePath(fileName + QStringLiteral(".tmp"));
        QFile temp(tempPath);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return {};
        temp.write(text.toUtf8());
        temp.close();
        QFile::remove(path);
        if (!QFile::rename(tempPath, path))
            return {};
        return path;
    }
};

void ThemeFileWatcherTest::emitsReloadRequestedForFileRewrite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(dir.path(), QStringLiteral("theme.json"), QStringLiteral("{}"));

    ThemeFileWatcher watcher;
    watcher.setEnabled(true);
    watcher.setWatchedFile(path);
    QSignalSpy spy(&watcher, &ThemeFileWatcher::reloadRequested);

    QVERIFY(!writeFile(dir.path(), QStringLiteral("theme.json"), QStringLiteral("{\"a\":1}")).isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);
}

void ThemeFileWatcherTest::emitsReloadRequestedForAtomicReplace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeFile(dir.path(), QStringLiteral("theme.json"), QStringLiteral("{}"));

    ThemeFileWatcher watcher;
    watcher.setEnabled(true);
    watcher.setWatchedFile(path);
    QSignalSpy spy(&watcher, &ThemeFileWatcher::reloadRequested);

    QVERIFY(!atomicReplace(dir.path(), QStringLiteral("theme.json"), QStringLiteral("{\"a\":2}")).isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 2000);
}

QTEST_MAIN(ThemeFileWatcherTest)

#include "tst_theme_file_watcher.moc"
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --target tst_theme_file_watcher --parallel
ctest --test-dir build --output-on-failure -R ThemeFileWatcher
```

Expected: build fails because `ThemeFileWatcher.h` does not exist.

- [ ] **Step 3: Implement ThemeFileWatcher**

Create `src/theme/ThemeFileWatcher.h`:

```cpp
#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

class ThemeFileWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ThemeFileWatcher(QObject* parent = nullptr);

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setWatchedFile(const QString& path);
    QString watchedFile() const { return m_watchedFile; }

    void clear();

signals:
    void reloadRequested();

private:
    void configure();
    void clearWatchPaths();
    void scheduleReload();
    void pollFileTimestamp();

    bool m_enabled = false;
    QString m_watchedFile;
    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    QTimer m_pollTimer;
    qint64 m_lastModifiedMs = 0;
};
```

Create `src/theme/ThemeFileWatcher.cpp` by moving the existing watcher logic out of `ComponentTheme.cpp`. Preserve these rules:

- debounce timer interval: `80`
- poll timer interval: `100`
- watch both file path and parent directory
- start poll fallback only when both file and directory watcher registration fail
- `clear()` removes watched files/directories and stops both timers

- [ ] **Step 4: Update ComponentTheme to delegate watcher work**

In `ComponentTheme.h`, replace watcher members:

```cpp
#include "ThemeFileWatcher.h"

ThemeFileWatcher m_themeWatcher;
```

Remove:

```cpp
QFileSystemWatcher m_themeWatcher;
QTimer m_reloadDebounceTimer;
QTimer m_themePollTimer;
qint64 m_currentThemeFileLastModifiedMs = 0;
void clearThemeWatcher();
void configureThemeWatcher();
void scheduleThemeReload();
```

In `ComponentTheme` constructor:

```cpp
connect(&m_themeWatcher, &ThemeFileWatcher::reloadRequested,
        this, [this]() { reloadCurrentTheme(); });
```

In `setHotReloadEnabled()`:

```cpp
m_hotReloadEnabled = enabled;
m_themeWatcher.setEnabled(m_hotReloadEnabled);
if (m_hotReloadEnabled) {
    m_themeWatcher.setWatchedFile(m_currentThemeFile);
} else {
    m_themeWatcher.clear();
}
emit hotReloadEnabledChanged();
```

In `applyTokens()`:

```cpp
setLastError(QString());
m_themeWatcher.setEnabled(m_hotReloadEnabled);
m_themeWatcher.setWatchedFile(m_currentThemeFile);
```

In `applyDark()`, `applyLight()`, `setAccent()`, `setButtonRadius()`, `setFontFamily()`:

```cpp
m_currentThemeFile.clear();
m_themeWatcher.clear();
```

- [ ] **Step 5: Wire CMake and tests**

Modify `CMakeLists.txt`:

```cmake
set(QTC_PUBLIC_HEADERS
    src/theme/ComponentTheme.h
    src/theme/ThemeFileWatcher.h
    src/theme/ThemeJsonLoader.h
    src/theme/ThemeTokens.h
    ...
)

set(QTC_CPP_SOURCES
    src/theme/ComponentTheme.cpp
    src/theme/ThemeFileWatcher.cpp
    src/theme/ThemeJsonLoader.cpp
    ...
)
```

Modify `tests/CMakeLists.txt`:

```cmake
qt_add_executable(tst_theme_file_watcher
    tst_theme_file_watcher.cpp
)

target_link_libraries(tst_theme_file_watcher PRIVATE
    QtQuickComponents
    Qt6::Test
)

add_test(NAME ThemeFileWatcher COMMAND tst_theme_file_watcher)
```

- [ ] **Step 6: Run tests**

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "ThemeFileWatcher|ComponentTheme"
ctest --test-dir build --output-on-failure
```

Expected: all QtQuickComponents tests pass.

- [ ] **Step 7: Commit**

```bash
cd /Users/zs/Downloads/QtQuickComponents
git add CMakeLists.txt src/theme/ComponentTheme.cpp src/theme/ComponentTheme.h src/theme/ThemeFileWatcher.cpp src/theme/ThemeFileWatcher.h tests/CMakeLists.txt tests/tst_theme_file_watcher.cpp
git commit -m "[重构] 拆分主题热加载监听"
```

---

### Task 3: Make JSON Themes The Canonical Default Source

**Files:**

- Modify: `/Users/zs/Downloads/QtQuickComponents/CMakeLists.txt`
- Modify: `/Users/zs/Downloads/QtQuickComponents/src/theme/ThemeJsonLoader.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/src/theme/ComponentTheme.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/tests/tst_theme_json_loader.cpp`
- Modify: `/Users/zs/Downloads/QtQuickComponents/tests/tst_component_theme.cpp`

- [ ] **Step 1: Add tests for built-in JSON fallback**

In `tests/tst_theme_json_loader.cpp`, add:

```cpp
void ThemeJsonLoaderTest::loadsBuiltInDarkTheme()
{
    ThemeLoadResult result = ThemeJsonLoader::loadBuiltInTheme(QStringLiteral("dark"));

    QVERIFY(result.ok);
    QCOMPARE(result.tokens.id, QStringLiteral("dark"));
    QCOMPARE(result.tokens.accent, QColor(QStringLiteral("#7c6fff")));
}

void ThemeJsonLoaderTest::rejectsUnknownBuiltInTheme()
{
    ThemeLoadResult result = ThemeJsonLoader::loadBuiltInTheme(QStringLiteral("missing-theme"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("missing-theme")));
}
```

Add both slots to the class declaration.

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --target tst_theme_json_loader --parallel
ctest --test-dir build --output-on-failure -R ThemeJsonLoader
```

Expected: `loadsBuiltInDarkTheme` fails because built-in JSON resources are not wired yet.

- [ ] **Step 3: Add theme JSON files to Qt resources**

Modify `CMakeLists.txt` near `qt_add_qml_module`:

```cmake
set_source_files_properties(
    themes/dark.json PROPERTIES QT_RESOURCE_ALIAS themes/dark.json
)
set_source_files_properties(
    themes/light.json PROPERTIES QT_RESOURCE_ALIAS themes/light.json
)

qt_add_qml_module(QtQuickComponents
    URI     "QuickUI.Components"
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    OUTPUT_DIRECTORY "${QTC_QML_MODULE_PATH}"
    SOURCES ${QTC_PUBLIC_HEADERS} ${QTC_CPP_SOURCES}
    QML_FILES ${QTC_QML_FILES}
    RESOURCES ${QTC_THEME_FILES}
)
```

- [ ] **Step 4: Implement built-in loader**

In `ThemeJsonLoader.cpp`:

```cpp
QStringList ThemeJsonLoader::builtInThemeIds()
{
    return { QStringLiteral("dark"), QStringLiteral("light") };
}

ThemeLoadResult ThemeJsonLoader::loadBuiltInTheme(const QString& themeId)
{
    const QString normalized = themeId.trimmed().toLower();
    if (!isValidThemeId(normalized)) {
        ThemeLoadResult result;
        result.error = QObject::tr("Invalid built-in theme id '%1'").arg(themeId);
        return result;
    }

    const QString resourcePath =
        QStringLiteral(":/qt/qml/QuickUI/Components/themes/%1.json").arg(normalized);
    ThemeLoadResult result = loadFile(resourcePath);
    if (!result.ok) {
        result.error = QObject::tr("Built-in theme '%1' was not found or invalid: %2")
                           .arg(normalized, result.error);
    }
    return result;
}
```

If this resource path does not exist after build, inspect generated qrc with:

```bash
cd /Users/zs/Downloads/QtQuickComponents
rg "dark.json" build
```

Use the generated path shown by `rg`, then update the string and rerun `ThemeJsonLoader`.

- [ ] **Step 5: Replace applyDark/applyLight token duplication**

In `ComponentTheme::ComponentTheme()`, replace direct `applyDark()` with:

```cpp
const ThemeLoadResult result = ThemeJsonLoader::loadBuiltInTheme(QStringLiteral("dark"));
if (result.ok) {
    applyTokens(result.tokens, Dark, QString());
}
```

In `loadTheme()` fallback path:

```cpp
const ThemeLoadResult builtIn = ThemeJsonLoader::loadBuiltInTheme(normalizedId);
if (builtIn.ok) {
    applyTokens(builtIn.tokens, normalizedId == QStringLiteral("light") ? Light : Dark, QString());
    emit styleChanged();
    return true;
}
```

Keep `applyDark()` / `applyLight()` only as thin wrappers or remove them if no call sites remain. If wrappers remain, they must call `ThemeJsonLoader::loadBuiltInTheme()` instead of hard-coding token values.

- [ ] **Step 6: Run tests**

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "ThemeJsonLoader|ComponentTheme"
ctest --test-dir build --output-on-failure
```

Expected: all QtQuickComponents tests pass; `dark.json` and `light.json` are now canonical defaults.

- [ ] **Step 7: Commit**

```bash
cd /Users/zs/Downloads/QtQuickComponents
git add CMakeLists.txt src/theme/ThemeJsonLoader.cpp src/theme/ComponentTheme.cpp tests/tst_theme_json_loader.cpp tests/tst_component_theme.cpp
git commit -m "[重构] 使用 JSON 作为默认主题来源"
```

---

### Task 4: Introduce PluginBased AppThemeService

**Files:**

- Create: `/Users/zs/Downloads/PluginBased/app/AppThemeService.h`
- Create: `/Users/zs/Downloads/PluginBased/app/AppThemeService.cpp`
- Modify: `/Users/zs/Downloads/PluginBased/app/AppController.cpp`
- Modify: `/Users/zs/Downloads/PluginBased/app/CMakeLists.txt`
- Create: `/Users/zs/Downloads/PluginBased/tests/app_theme_service_checks.py`
- Modify: `/Users/zs/Downloads/PluginBased/CMakeLists.txt`

- [ ] **Step 1: Add architecture check test**

Create `tests/app_theme_service_checks.py`:

```python
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    service_h = read("app/AppThemeService.h")
    service_cpp = read("app/AppThemeService.cpp")
    controller_cpp = read("app/AppController.cpp")

    require("class AppThemeService" in service_h, "AppThemeService should exist")
    require("applyTheme" in service_h, "AppThemeService should expose applyTheme")
    require("themeDirectoryCandidates" in service_h, "theme directory resolution should be in AppThemeService")
    require("ComponentTheme::instance().loadTheme" in service_cpp, "AppThemeService should load ComponentTheme")
    require("ComponentTheme::instance().loadTheme" not in controller_cpp, "AppController should not load ComponentTheme directly")
    require("setHotReloadEnabled" not in controller_cpp, "AppController should not configure ComponentTheme hot reload directly")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Wire test and verify it fails**

Modify `CMakeLists.txt` test section:

```cmake
add_test(NAME app_theme_service_checks
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/app_theme_service_checks.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
```

Run:

```bash
cd /Users/zs/Downloads/PluginBased
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R app_theme_service_checks
```

Expected: test fails because `app/AppThemeService.h` does not exist.

- [ ] **Step 3: Add AppThemeService API**

Create `app/AppThemeService.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>

struct ThemeApplyResult
{
    QString requestedTheme;
    QString appliedTheme;
    QString themeDirectory;
    QString error;
    bool usedFallback = false;
};

class AppThemeService
{
public:
    static AppThemeService& instance();

    QStringList themeDirectoryCandidates() const;
    QString resolveThemeDirectory() const;
    void configure();
    ThemeApplyResult applyTheme(const QString& themeName);

private:
    AppThemeService() = default;

    bool m_configured = false;
    QString m_themeDirectory;
};
```

Create `app/AppThemeService.cpp`:

```cpp
#include "AppThemeService.h"

#include "AppConfig.h"
#include "ComponentTheme.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QDir>

AppThemeService& AppThemeService::instance()
{
    static AppThemeService service;
    return service;
}

QStringList AppThemeService::themeDirectoryCandidates() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        appDir + "/themes",
        appDir + "/../themes",
        appDir + "/../../../../themes",
        appDir + "/../Resources/themes",
    };
}

QString AppThemeService::resolveThemeDirectory() const
{
    const QStringList candidates = themeDirectoryCandidates();
    for (const QString& candidate : candidates) {
        const QString clean = QDir::cleanPath(candidate);
        if (QDir(clean).exists())
            return clean;
    }
    return QDir::cleanPath(candidates.first());
}

void AppThemeService::configure()
{
    if (m_configured)
        return;

    m_themeDirectory = resolveThemeDirectory();
    ComponentTheme::instance().setThemeDirectory(m_themeDirectory);
    ComponentTheme::instance().setHotReloadEnabled(true);
    LOG_INFO("AppThemeService: theme dir = {}", m_themeDirectory.toStdString());
    m_configured = true;
}

ThemeApplyResult AppThemeService::applyTheme(const QString& themeName)
{
    configure();

    ThemeApplyResult result;
    result.requestedTheme = themeName;
    result.themeDirectory = m_themeDirectory;

    AppConfig::instance().setThemeName(themeName);
    QString next = AppConfig::instance().themeName();

    if (!ComponentTheme::instance().loadTheme(next)) {
        result.usedFallback = true;
        result.error = ComponentTheme::instance().lastError();
        LOG_WARN("AppThemeService: failed to load theme '{}': {}",
                 next.toStdString(),
                 result.error.toStdString());
        next = QStringLiteral("dark");
        AppConfig::instance().setThemeName(next);
        ComponentTheme::instance().loadTheme(next);
    }

    AppConfig::instance().save();
    result.appliedTheme = next;
    return result;
}
```

- [ ] **Step 4: Wire AppController to AppThemeService**

Modify `app/AppController.cpp` includes:

```cpp
#include "AppThemeService.h"
```

Remove:

```cpp
#include "ComponentTheme.h"
#include <QDir>
```

Remove anonymous namespace functions `themeDirectoryCandidates()`, `resolveThemeDirectory()`, `configureThemeSystem()`.

In `initPlugins()`, replace:

```cpp
configureThemeSystem();
setTheme(AppConfig::instance().themeName());
```

with:

```cpp
setTheme(AppConfig::instance().themeName());
```

In `setTheme()`:

```cpp
void AppController::setTheme(const QString& themeName)
{
    const ThemeApplyResult result = AppThemeService::instance().applyTheme(themeName);

    if (m_currentTheme == result.appliedTheme)
        return;

    m_currentTheme = result.appliedTheme;
    emit currentThemeChanged();
}
```

- [ ] **Step 5: Wire CMake**

Modify `app/CMakeLists.txt`:

```cmake
qt_add_executable(PluginBasedApp
    main.cpp
    AppConfig.h
    AppConfig.cpp
    AppThemeService.h
    AppThemeService.cpp
    CrashHandler.h
    CrashHandler.cpp
)

qt_add_qml_module(PluginBasedApp
    ...
    SOURCES
        AppController.h     AppController.cpp
        AppThemeService.h   AppThemeService.cpp
        ../core/PluginManager.h  ../core/PluginManager.cpp
)
```

- [ ] **Step 6: Run tests**

```bash
cd /Users/zs/Downloads/PluginBased
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "app_theme_service_checks|ci_ctest_checks"
ctest --test-dir build --output-on-failure
```

Expected: all PluginBased tests pass.

- [ ] **Step 7: Commit**

```bash
cd /Users/zs/Downloads/PluginBased
git add CMakeLists.txt app/CMakeLists.txt app/AppController.cpp app/AppThemeService.cpp app/AppThemeService.h tests/app_theme_service_checks.py
git commit -m "[重构] 拆分宿主主题服务"
```

---

### Task 5: Define Runtime Theme Resource Contract

**Files:**

- Modify: `/Users/zs/Downloads/PluginBased/CMakeLists.txt`
- Modify: `/Users/zs/Downloads/PluginBased/app/AppThemeService.cpp`
- Modify: `/Users/zs/Downloads/PluginBased/README.md`
- Modify: `/Users/zs/Downloads/PluginBased/tests/ci_ctest_checks.py`

- [ ] **Step 1: Add test for explicit runtime resource contract**

Modify `tests/ci_ctest_checks.py`:

```python
root_cmake = read("CMakeLists.txt")
app_theme_service_cpp = read("app/AppThemeService.cpp")
readme = read("README.md")

require("QTC_THEME_OUTPUT_DIR" in root_cmake or "themes" in root_cmake,
        "CMake should define how theme JSON files are copied for runtime")
require("Contents/Resources/themes" in app_theme_service_cpp,
        "AppThemeService should support macOS bundle Resources/themes")
require("<app>/themes" in readme,
        "README should document runtime theme directory contract")
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd /Users/zs/Downloads/PluginBased
ctest --test-dir build --output-on-failure -R ci_ctest_checks
```

Expected: fails because README does not document `<app>/themes` contract yet.

- [ ] **Step 3: Copy default themes into PluginBased runtime output**

Modify root `CMakeLists.txt` after `FetchContent_MakeAvailable(QtQuickComponents)`:

```cmake
set(PLUGINBASED_THEME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/themes")
add_custom_target(PluginBased_copy_themes ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGINBASED_THEME_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/../QtQuickComponents/themes"
            "${PLUGINBASED_THEME_OUTPUT_DIR}"
    COMMENT "Copying PluginBased runtime theme JSON files"
)
add_dependencies(PluginBasedApp PluginBased_copy_themes)

install(DIRECTORY "${PLUGINBASED_THEME_OUTPUT_DIR}/"
    DESTINATION "themes"
    FILES_MATCHING PATTERN "*.json"
)
```

If `PluginBasedApp` is not defined yet at this point, move `add_dependencies(PluginBasedApp PluginBased_copy_themes)` to after `add_subdirectory(app)`.

- [ ] **Step 4: Clarify AppThemeService candidates**

Keep `AppThemeService::themeDirectoryCandidates()` ordered as:

```cpp
return {
    appDir + "/themes",
    appDir + "/../Resources/themes",
    appDir + "/../themes",
    appDir + "/../../../../themes",
};
```

This makes packaged app resources win over broad development fallbacks.

- [ ] **Step 5: Document contract**

Update README theme section:

```markdown
运行时主题目录约定：

- 开发构建：`<build>/themes`
- 普通发布：`<app>/themes`
- macOS bundle：`PluginBasedApp.app/Contents/Resources/themes`

宿主优先加载运行时目录中的 `themes/<id>.json`，找不到时由 QtQuickComponents 内置 `dark` / `light` JSON 作为 fallback。
```

- [ ] **Step 6: Run tests**

```bash
cd /Users/zs/Downloads/PluginBased
cmake --build build --parallel
ctest --test-dir build --output-on-failure -R "ci_ctest_checks|app_theme_service_checks"
ctest --test-dir build --output-on-failure
```

Expected: all PluginBased tests pass.

- [ ] **Step 7: Commit**

```bash
cd /Users/zs/Downloads/PluginBased
git add CMakeLists.txt README.md app/AppThemeService.cpp tests/ci_ctest_checks.py
git commit -m "[功能修改] 明确主题资源运行时契约"
```

---

### Task 6: Replace Implementation-Detail Tests With Behavior/Architecture Checks

**Files:**

- Modify: `/Users/zs/Downloads/PluginBased/tests/ci_ctest_checks.py`
- Modify: `/Users/zs/Downloads/PluginBased/tests/app_theme_service_checks.py`
- Modify: `/Users/zs/Downloads/PluginBased/README.md`

- [ ] **Step 1: Remove fragile string checks**

In `tests/ci_ctest_checks.py`, remove checks that require exact implementation calls:

```python
require("setThemeDirectory" in app_controller_cpp, ...)
require("setHotReloadEnabled(true)" in app_controller_cpp, ...)
require("ComponentTheme::instance().loadTheme" in app_controller_cpp, ...)
require("themeDirectoryCandidates" in app_controller_cpp, ...)
```

Replace with:

```python
app_theme_service_h = read("app/AppThemeService.h")
app_theme_service_cpp = read("app/AppThemeService.cpp")

require("class AppThemeService" in app_theme_service_h,
        "Theme orchestration should live in AppThemeService")
require("ThemeApplyResult" in app_theme_service_h,
        "Theme application should return a structured result")
require("ComponentTheme::instance().loadTheme" in app_theme_service_cpp,
        "AppThemeService should apply themes through ComponentTheme")
require("ComponentTheme::instance().loadTheme" not in app_controller_cpp,
        "AppController should delegate theme application to AppThemeService")
```

- [ ] **Step 2: Keep UI token checks**

Keep checks that validate QML usage of `ComponentTheme.surface`, `textPrimary`, `textSecondary`, `accent`, because these are architectural contracts for plugins and host UI.

- [ ] **Step 3: Run tests**

```bash
cd /Users/zs/Downloads/PluginBased
ctest --test-dir build --output-on-failure -R "ci_ctest_checks|app_theme_service_checks"
ctest --test-dir build --output-on-failure
```

Expected: all PluginBased tests pass.

- [ ] **Step 4: Commit**

```bash
cd /Users/zs/Downloads/PluginBased
git add tests/ci_ctest_checks.py tests/app_theme_service_checks.py README.md
git commit -m "[测试] 调整主题架构检查"
```

---

### Task 7: Full Verification And Push

**Files:**

- Verify only; no source edits expected.

- [ ] **Step 1: Verify QtQuickComponents**

```bash
cd /Users/zs/Downloads/QtQuickComponents
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed
```

- [ ] **Step 2: Verify PluginBased**

```bash
cd /Users/zs/Downloads/PluginBased
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed
```

- [ ] **Step 3: Review uncommitted files**

```bash
cd /Users/zs/Downloads/QtQuickComponents
git status --short

cd /Users/zs/Downloads/PluginBased
git status --short
```

Expected:

- QtQuickComponents may still show unrelated `.gitignore` if it remains user-owned.
- PluginBased may still show unrelated local files: `6月29日.mov`, `plugins/demoPlugin/`, and untracked design/spec docs if intentionally not committed.
- No source file from this refactor should remain unstaged or uncommitted.

- [ ] **Step 4: Push**

```bash
cd /Users/zs/Downloads/QtQuickComponents
git push origin main

cd /Users/zs/Downloads/PluginBased
git pull --rebase origin main
git push origin main
```

Expected: both repositories push successfully.

---

## Self-Review

Spec coverage:

- `ComponentTheme` 职责过重：Task 1 and Task 2 split JSON loading and watcher.
- 路径契约不清：Task 5 defines runtime theme contract and CMake copy/install behavior.
- `AppController` 耦合：Task 4 introduces `AppThemeService`.
- 默认主题重复定义：Task 3 makes JSON the canonical default source.
- 测试脆弱：Task 6 replaces implementation-detail assertions.

Type consistency:

- `ThemeTokens`, `ThemeLoadResult`, `ThemeJsonLoader`, `ThemeFileWatcher`, `ThemeApplyResult`, and `AppThemeService` are introduced before later tasks reference them.
- `ComponentTheme` public QML API remains unchanged.
- PluginBased continues to expose `AppController.currentTheme`, `setTheme()`, and `toggleTheme()` to QML.

Verification commands:

- QtQuickComponents: `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
- PluginBased: `cmake --build build --parallel`, `ctest --test-dir build --output-on-failure`.
