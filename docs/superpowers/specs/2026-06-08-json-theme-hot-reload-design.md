# JSON 主题热加载设计

## 背景

第一阶段已经让 `PluginBased` 复用 `QtQuickComponents` 的 `ComponentTheme`，支持内置 `Dark / Light` 切换，并把当前选择持久化到 `pluginbased.ini` 的 `[ui] theme`。

第二阶段需要把主题从 C++ 内置枚举扩展为可外部配置的 JSON 主题文件，并支持修改主题文件后即时刷新 UI。主题系统仍然以 `QtQuickComponents::ComponentTheme` 为唯一来源，宿主和插件都继续通过 `ComponentTheme` token 取色、字号、尺寸和动效。

## 目标

1. 在 `QtQuickComponents` 中增加 JSON 主题加载能力。
2. 支持从 `themes/*.json` 加载主题。
3. 使用 `QFileSystemWatcher` 监听当前主题文件。
4. 修改当前主题 JSON 后自动重新加载并触发 QML 刷新。
5. JSON 无效时保留旧主题，不把 UI 刷成半套状态。
6. `PluginBased` 继续使用 `[ui] theme` 保存当前主题 ID。
7. 现有 `ComponentTheme.Dark / Light / Custom` 基本兼容保留。

## 非目标

1. 本阶段不做完整主题管理 UI。
2. 本阶段不做插件自定义主题扩展 token。
3. 本阶段不做远程主题下载。
4. 本阶段不做主题 marketplace。
5. 本阶段不要求用户在界面里编辑 JSON。

## 主题文件格式

主题文件放在 `themes/` 目录，文件名推荐和主题 ID 一致：

```text
themes/
  dark.json
  light.json
  enterprise-blue.json
```

JSON 格式：

```json
{
  "id": "dark",
  "name": "Dark",
  "colors": {
    "accent": "#7c6fff",
    "accentHover": "#9d90ff",
    "accentPressed": "#9d90ff",
    "accentDisabled": "#44445a",
    "iconColor": "#ffffff99",
    "iconColorPressed": "#ffffffcc",
    "buttonHover": "#ffffff0e",
    "buttonPressed": "#ffffff18",
    "trackBg": "#2a2a3a",
    "trackBuffer": "#ffffff12",
    "handleBorder": "#ffffff30",
    "textPrimary": "#f0f0f5",
    "textSecondary": "#9090a8",
    "textDisabled": "#50505f",
    "textOnAccent": "#ffffff",
    "surface": "#1e1e2a",
    "surfaceHover": "#26263a",
    "separator": "#ffffff14",
    "inputBg": "#14141e",
    "inputBorder": "#ffffff20",
    "inputFocus": "#7c6fff",
    "inputText": "#f0f0f5",
    "inputPlaceholder": "#50505f"
  },
  "sizes": {
    "buttonSize": 34,
    "buttonRadius": 6,
    "inputHeight": 36,
    "inputRadius": 6,
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
}
```

规则：

- `id` 必须非空，只允许字母、数字、`-`、`_`。
- `colors` 中颜色值必须是 Qt 可解析的颜色字符串。
- `sizes` 和 `fonts` 中的数值必须为正数。
- `motion.durationFast` 和 `motion.durationNormal` 必须大于等于 0。
- `motion.reducedMotion` 为布尔值。
- 缺失必需字段时加载失败。

本阶段采用“完整主题文件”策略，不支持部分覆盖。这样可以避免某个 JSON 忘写字段时继承旧主题残留状态。

## ComponentTheme API 设计

在 `QtQuickComponents/src/theme/ComponentTheme.h` 中新增：

```cpp
Q_PROPERTY(QString themeId READ themeId NOTIFY styleChanged)
Q_PROPERTY(QString themeName READ themeName NOTIFY styleChanged)
Q_PROPERTY(QString themeDirectory READ themeDirectory WRITE setThemeDirectory NOTIFY themeDirectoryChanged)
Q_PROPERTY(bool hotReloadEnabled READ hotReloadEnabled WRITE setHotReloadEnabled NOTIFY hotReloadEnabledChanged)
Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

QString themeId() const;
QString themeName() const;
QString themeDirectory() const;
void setThemeDirectory(const QString& directory);

bool hotReloadEnabled() const;
void setHotReloadEnabled(bool enabled);

QString lastError() const;

Q_INVOKABLE bool loadTheme(const QString& themeId);
Q_INVOKABLE bool loadThemeFile(const QString& path);
Q_INVOKABLE bool reloadCurrentTheme();
Q_INVOKABLE QStringList availableThemes() const;
```

新增信号：

```cpp
void themeDirectoryChanged();
void hotReloadEnabledChanged();
void lastErrorChanged();
```

保留现有：

```cpp
Q_PROPERTY(Style style READ style WRITE setStyle NOTIFY styleChanged)
Q_INVOKABLE void setAccent(const QColor& c);
Q_INVOKABLE void setButtonRadius(int r);
Q_INVOKABLE void setFontFamily(const QString& family);
```

兼容策略：

- `setStyle(Dark)` 等价于加载内置 `dark` 主题 token。
- `setStyle(Light)` 等价于加载内置 `light` 主题 token。
- `loadTheme("dark")` 优先从 `themeDirectory/dark.json` 加载；不存在时回退内置 dark。
- `loadTheme("light")` 优先从 `themeDirectory/light.json` 加载；不存在时回退内置 light。
- `loadThemeFile(path)` 成功后 `style` 置为 `Custom`，并记录当前主题文件。
- `setAccent()`、`setButtonRadius()`、`setFontFamily()` 继续进入 `Custom`，但不会自动写回 JSON。

## 内部数据流

### 加载主题

1. `loadTheme(themeId)` 规范化 ID。
2. 拼接 `themeDirectory + "/" + themeId + ".json"`。
3. 如果文件存在，调用 `loadThemeFile(path)`。
4. 如果文件不存在且 ID 是 `dark` / `light`，使用内置主题。
5. 如果文件不存在且不是内置主题，返回 `false`，设置 `lastError`。

### 解析 JSON

1. 读取文件。
2. `QJsonDocument::fromJson()` 解析。
3. 校验对象结构和必需字段。
4. 先写入临时 `ThemeTokens` 结构。
5. 全部校验通过后一次性应用到成员变量。
6. 更新 `themeId`、`themeName`、当前文件路径。
7. 发射 `styleChanged()`。

关键点：解析和校验阶段不能直接写 `m_accent` 等成员，避免坏 JSON 导致部分 token 被污染。

### 热加载

内部使用：

```cpp
QFileSystemWatcher m_themeWatcher;
QTimer m_reloadDebounceTimer;
```

流程：

1. `loadThemeFile(path)` 成功后，如果 `hotReloadEnabled` 为 true，监听该文件。
2. 文件变化后启动 debounce timer，例如 80ms。
3. timer 触发后调用 `reloadCurrentTheme()`。
4. 重新加载成功则刷新 UI。
5. 重新加载失败则保留旧主题，更新 `lastError`。
6. 某些平台保存文件会导致 watcher 丢失路径，因此 reload 后重新添加 watch path。

## CMake 和资源部署

在 `QtQuickComponents` 仓库新增：

```text
themes/
  dark.json
  light.json
```

CMake 负责：

1. 开发构建时复制 `themes/` 到 `${QTC_QML_OUTPUT_BASE}/themes` 或调用方指定的输出目录。
2. install 时安装到 `${CMAKE_INSTALL_DATADIR}/QtQuickComponents/themes`。

`PluginBased` 使用 FetchContent 引入组件库时，需要在自己的构建目录可找到：

```text
build/themes/dark.json
build/themes/light.json
```

如果 `QtQuickComponents` 的主题目录输出在组件库自身位置，`PluginBased` 启动时要显式设置：

```cpp
ComponentTheme::instance().setThemeDirectory(themeDir);
```

推荐 `PluginBased` 的开发构建结构：

```text
build/
  themes/
    dark.json
    light.json
  PluginBasedApp.app/
  plugins/
```

发布包结构：

```text
PluginBased/
  themes/
    dark.json
    light.json
  plugins/
  PluginBasedApp
```

## PluginBased 接入

第一阶段已有：

```ini
[ui]
theme=dark
```

第二阶段继续使用这个字段，含义从 enum 名称扩展为 theme ID。

启动流程：

1. `AppConfig` 加载 `[ui] theme`。
2. `AppController` 或启动阶段计算主题目录。
3. 调用 `ComponentTheme::instance().setThemeDirectory(themeDir)`。
4. 调用 `ComponentTheme::instance().setHotReloadEnabled(true)`。
5. 调用 `ComponentTheme::instance().loadTheme(AppConfig::instance().themeName())`。
6. 如果失败，记录日志并回退 `dark`。

`toggleTheme()` 仍然只在 `dark` / `light` 间切换，后续主题列表 UI 再扩展。

## 错误处理

| 场景 | 行为 |
|---|---|
| 主题目录不存在 | `loadTheme()` 对内置 dark/light 回退内置 token，其他主题失败 |
| JSON 语法错误 | 保留旧主题，设置 `lastError` |
| 颜色字段非法 | 保留旧主题，设置 `lastError` |
| 数值字段非法 | 保留旧主题，设置 `lastError` |
| 当前主题文件被删除 | 保留旧主题，设置 `lastError` |
| 文件保存触发多次 changed | debounce 后只 reload 一次 |
| watcher 路径丢失 | reload 后重新 watch 当前文件 |

## 测试策略

### QtQuickComponents 测试

新增或扩展 C++ 测试：

1. `loadThemeFile()` 能加载完整 JSON。
2. 加载后所有关键 token 更新。
3. 非法 JSON 返回 false，旧 token 不变。
4. 非法颜色返回 false，旧 token 不变。
5. `loadTheme("dark")` 能从主题目录加载 `dark.json`。
6. `availableThemes()` 返回 `themes/*.json` 的 ID 列表。
7. 热加载：修改当前主题文件后，`styleChanged()` 触发且 token 更新。

### PluginBased 测试

更新结构检查：

1. `PluginBased` 调用 `setThemeDirectory()`。
2. `PluginBased` 调用 `setHotReloadEnabled(true)`。
3. `PluginBased` 调用 `loadTheme(AppConfig::themeName())`。
4. README 说明 JSON 主题和热加载路径。

## 分阶段实施任务

1. 在 `QtQuickComponents` 设计 `ThemeTokens` 内部结构和 JSON parser。
2. 增加 `ComponentTheme` JSON 加载 API。
3. 增加 `themes/dark.json` 和 `themes/light.json`。
4. 增加 CMake 复制和安装主题目录。
5. 增加 QFileSystemWatcher + debounce 热加载。
6. 增加 `QtQuickComponents` 单元测试。
7. 在 `PluginBased` 设置主题目录、启用热加载并调用 `loadTheme()`。
8. 更新 README 和结构检查。
9. 运行 `QtQuickComponents` 测试。
10. 运行 `PluginBased` configure、build、CTest。

## 风险与处理

| 风险 | 处理 |
|---|---|
| JSON 字段多，解析代码膨胀 | 使用内部 `ThemeTokens` 和小型解析 helper，避免直接在 `loadThemeFile()` 写长逻辑 |
| 坏 JSON 导致 UI 半更新 | 先解析到临时结构，全部校验通过后一次性 apply |
| QFileSystemWatcher 在保存时丢 watch | reload 后重新 addPath |
| 主题目录在开发和发布结构不同 | `PluginBased` 负责计算并设置 `themeDirectory` |
| 与现有 Dark/Light API 不兼容 | 保留 `setStyle()`，并让它映射到内置 token |
| 插件没有跟随主题 | 插件继续使用 `ComponentTheme`，不增加插件私有主题 |
