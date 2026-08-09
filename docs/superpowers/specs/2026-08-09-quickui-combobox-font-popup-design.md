# QuickUI ComboBox 字体与弹层宽度设计

## Context

PlayPlugin 的播放倍率选择器在 macOS 上展开后，未选中项显示为明显拉散的 `0 . 5 x`、`1 . 2 5 x`。实际界面确认该控件解析为外部依赖 `QuickUI.Components.ComboBox`，而不是 `QtQuick.Controls.Basic.ComboBox`。当前 QuickUI 组件把主题中的空 `fontFamily` 直接写入根控件和 delegate，并分别重建 delegate 字体；同时弹层宽度固定绑定到控件宽度，迫使业务代码直接覆盖 `popup.width`。

PluginBased 通过 FetchContent 固定到 QtQuickComponents 提交 `8e376dfc50e703a49b4e66aa1302e5fcd6df2cde`。`build/_deps/qtquickcomponents-src` 是生成依赖目录，只用于取证和构建，不作为源码修改位置。

## Goals

- 主题字体为空时，ComboBox 明确使用应用默认字体。
- 根控件、当前值和所有 delegate 使用同一字体族与字号基线；选中态只改变字重和颜色。
- ComboBox 提供稳定的 `popupMinimumWidth` 公共属性，业务代码不再访问 `popup.width` 内部实现。
- 倍率选择器收起宽度保持 76，弹层最小宽度为 112，六个倍率完整且紧凑显示。
- QtQuickComponents 和 PluginBased 均有自动化回归覆盖，并完成真实界面验收。

## Non-Goals

- 不修改所有 QuickUI 控件或全局 ComponentTheme 的字体策略。
- 不在 PlayPlugin 中复制 ComboBox delegate、背景或弹层实现。
- 不改变倍率集合、选中行为、键盘导航或播放速率业务逻辑。
- 不修改 `build/`、`build/_deps/` 中的生成文件。

## Current Design

`ControlBar.qml` 使用 `QuickUI.Components.ComboBox`。组件当前行为如下：

- 根控件：`font.family: ComponentTheme.fontFamily`。
- delegate：再次使用 `ComponentTheme.fontFamily`、字号和按状态变化的字重。
- 弹层：`width: root.width`。
- PlayPlugin 临时使用 `popup.width: Math.max(rateSelector.width, 112)` 扩宽弹层。

主题 JSON 的 `fontFamily` 为空。Qt 的字体匹配会综合字体族和字重，且字体族是主要匹配条件；因此显式、稳定地解析应用字体族比依赖空字符串的跨平台匹配更可控。

## Proposed Design

### ComboBox 字体

在 `src/controls/ComboBox.qml` 中增加只读属性：

```qml
readonly property string effectiveFontFamily: ComponentTheme.fontFamily.length > 0
    ? ComponentTheme.fontFamily
    : Application.font.family
```

根控件绑定 `font.family: root.effectiveFontFamily`。delegate 的 `Text` 从 `root.font` 读取字体族和像素字号，仅保留已有的状态字重切换。这样主题提供字体时行为不变，主题留空时与应用默认字体一致，普通项和选中项不会走不同的空字体匹配路径。

`effectiveFontFamily` 是只读的 QML API，便于测试和诊断；现有调用方无需迁移。

### ComboBox 弹层宽度

增加：

```qml
property real popupMinimumWidth: root.width
```

弹层内部改为：

```qml
width: Math.max(root.width, root.popupMinimumWidth)
```

默认值维持现有行为。调用方只声明最小宽度，组件仍负责弹层的最终宽度和内部布局。

### PlayPlugin 集成

倍率选择器保留 `Layout.preferredWidth: 76`、`Layout.minimumWidth: 76`，使用 `popupMinimumWidth: 112`，删除对 `popup.width` 的直接覆盖。当前值的本地 contentItem/indicator 样式暂不重构，本次只消除异常菜单显示及内部弹层耦合。

### Ownership, Threading, API Compatibility

本变更只涉及 GUI 线程上的 QML 值属性，不新增 QObject、资源所有权、回调、锁或跨线程等待。`popupMinimumWidth` 和 `effectiveFontFamily` 都是向后兼容的新增 QML 属性；默认弹层宽度不变，不影响现有调用方。

## Alternatives

### 在 ControlBar 中自定义 delegate

能快速修复单个页面，但会复制 QuickUI 的字体、颜色、hover、选中背景和键盘语义，后续组件升级容易漂移，因此拒绝。

### 全局修改 ComponentTheme 字体回退

架构上更统一，但会改变所有 QuickUI 控件的字体选择，当前没有完成全组件视觉审计，风险超过本缺陷范围，因此拒绝。

### 仅强制 `font.letterSpacing: 0`

可能掩盖症状，但无法解决空字体族导致的字体匹配不一致，也可能覆盖应用字体本身的合法字距设置，因此拒绝。

## Verification

- QtQuickComponents `QmlComponents`：验证空主题字体回退到 `Application.font.family`，普通项与选中项字体族/字号一致。
- QtQuickComponents `QmlComponents`：验证弹层宽度为 `max(control.width, popupMinimumWidth)`。
- QtQuickComponents：运行聚焦 QML 测试、QML lint、完整 CTest 和构建。
- PluginBased：更新回归契约，重新配置后构建并运行完整 CTest。
- 手工启动 PluginBasedApp，打开倍率菜单，确认六个标签完整、字符间距正常，收起值仍完整显示。

## Rollout

1. 在独立 QtQuickComponents 工作区按 TDD 完成修复。
2. 验证并形成独立提交。
3. 推送前取得用户明确授权；只有提交在远端可达后，才更新 PluginBased 的 `GIT_TAG`。
4. 更新 PluginBased 集成配置并完成全量验证与界面验收。

若上游提交未推送，不把不可被干净克隆解析的本地哈希写入 PluginBased。
