# QuickUI ComboBox Font and Popup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复 QuickUI ComboBox 在空主题字体下的异常菜单字距，并提供由 PlayPlugin 使用的弹层最小宽度 API。

**Architecture:** 在独立 QtQuickComponents 源码工作区实现字体回退和 `popupMinimumWidth`，由组件自己的 QML 行为测试锁定。组件提交远端可达后，PluginBased 只更新 FetchContent pin 和倍率选择器配置，不复制组件内部实现。

**Tech Stack:** Qt 6、QML、Qt Quick Test、CMake/CTest、Git FetchContent。

## Global Constraints

- C++ 标准保持 C++17；本任务不改 C++ API、ABI、对象所有权或线程模型。
- 不修改 PluginBased 的 `build/`、`build/_deps/` 生成内容。
- 不覆盖 PluginBased 当前工作区中与本任务无关的用户改动。
- 倍率选择器收起宽度保持 76，弹层最小宽度为 112。
- 主题字体非空时继续使用主题字体；为空时使用 `Application.font.family`。
- 普通项和选中项仅在字重和颜色上不同，字体族与字号基线必须相同。
- 未获得推送授权前不得推送；上游提交远端可达前不得把本地哈希写入 PluginBased。

---

### Task 1: 建立独立 QtQuickComponents 工作区和干净基线

**Files:**
- Source: `/Users/zs/Downloads/PluginBased/build/_deps/qtquickcomponents-src`
- Create working copy: `/private/tmp/QtQuickComponents-combobox-fix`
- Build output: `/private/tmp/QtQuickComponents-combobox-build`

**Interfaces:**
- Consumes: QtQuickComponents 提交 `8e376dfc50e703a49b4e66aa1302e5fcd6df2cde`。
- Produces: 独立分支 `fix/combobox-font-popup-width` 和可重复的测试基线。

- [ ] **Step 1: 从本地 FetchContent checkout 创建非生成源码副本**

```bash
git clone --no-hardlinks /Users/zs/Downloads/PluginBased/build/_deps/qtquickcomponents-src /private/tmp/QtQuickComponents-combobox-fix
git -C /private/tmp/QtQuickComponents-combobox-fix remote set-url origin https://github.com/Zacus/QtQuickComponents.git
git -C /private/tmp/QtQuickComponents-combobox-fix switch -c fix/combobox-font-popup-width
```

- [ ] **Step 2: 配置测试构建**

```bash
cmake -S /private/tmp/QtQuickComponents-combobox-fix -B /private/tmp/QtQuickComponents-combobox-build -DCMAKE_BUILD_TYPE=Debug -DQTC_BUILD_TESTS=ON -DQTC_BUILD_EXAMPLES=OFF
cmake --build /private/tmp/QtQuickComponents-combobox-build --parallel
```

- [ ] **Step 3: 运行基线测试**

```bash
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build --output-on-failure
```

Expected: 全部现有测试通过；否则停止并报告基线失败。

### Task 2: 用 TDD 增加字体回退行为

**Files:**
- Modify: `/private/tmp/QtQuickComponents-combobox-fix/tests/qml/tst_controls.qml`
- Modify: `/private/tmp/QtQuickComponents-combobox-fix/src/controls/ComboBox.qml`

**Interfaces:**
- Consumes: `ComponentTheme.fontFamily`、`Application.font.family`、ComboBox 的 `font` 和 `delegate`。
- Produces: `readonly property string effectiveFontFamily`。

- [ ] **Step 1: 添加会捕获空字体匹配和 delegate 字体漂移的失败测试**

在 `tst_controls.qml` 中增加：

```qml
function test_comboBoxUsesApplicationFontWhenThemeFamilyIsEmpty() {
    var combo = createTemporaryObject(comboBoxComponent, this)
    verify(combo !== null)

    compare(ComponentTheme.fontFamily, "")
    compare(combo.effectiveFontFamily, Application.font.family)
    compare(combo.font.family, Application.font.family)

    var normal = combo.delegate.createObject(combo, { "index": 0 })
    var selected = combo.delegate.createObject(combo, { "index": 1 })
    verify(normal !== null)
    verify(selected !== null)
    compare(normal.contentItem.font.family, combo.font.family)
    compare(selected.contentItem.font.family, combo.font.family)
    compare(normal.contentItem.font.pixelSize, combo.font.pixelSize)
    compare(selected.contentItem.font.pixelSize, combo.font.pixelSize)
    normal.destroy()
    selected.destroy()
}
```

该测试应在缺少 `effectiveFontFamily` 时失败；它保护的生产缺陷是重新把空主题字体直接传给根控件或 delegate。

- [ ] **Step 2: 运行聚焦测试并确认 RED**

```bash
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build -R '^QmlComponents$' --output-on-failure
```

Expected: `test_comboBoxUsesApplicationFontWhenThemeFamilyIsEmpty` 因 `effectiveFontFamily` 不存在或不等于应用字体而失败。

- [ ] **Step 3: 实现最小字体回退**

在 ComboBox 根对象增加：

```qml
readonly property string effectiveFontFamily: ComponentTheme.fontFamily.length > 0
    ? ComponentTheme.fontFamily
    : Application.font.family
```

将根字体族改为：

```qml
font.family: root.effectiveFontFamily
```

将 delegate 字体族和字号改为：

```qml
font.family: root.font.family
font.pixelSize: root.font.pixelSize
```

保留原有按 selected/highlighted 切换的 `font.weight` 和颜色逻辑。

- [ ] **Step 4: 重新构建并确认 GREEN**

```bash
cmake --build /private/tmp/QtQuickComponents-combobox-build --parallel --target tst_qml_components
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build -R '^QmlComponents$' --output-on-failure
```

Expected: `QmlComponents` 通过。

### Task 3: 用 TDD 增加弹层最小宽度 API

**Files:**
- Modify: `/private/tmp/QtQuickComponents-combobox-fix/tests/qml/tst_controls.qml`
- Modify: `/private/tmp/QtQuickComponents-combobox-fix/src/controls/ComboBox.qml`

**Interfaces:**
- Consumes: ComboBox 控件 `width` 和内部 `popup.width`。
- Produces: `property real popupMinimumWidth`，默认等于控件宽度。

- [ ] **Step 1: 添加失败的弹层宽度行为测试**

给 `comboBoxComponent` 设置 `width: 76` 和 `popupMinimumWidth: 112`，并增加：

```qml
function test_comboBoxPopupHonorsMinimumWidth() {
    var combo = createTemporaryObject(comboBoxComponent, this)
    verify(combo !== null)

    compare(combo.popup.width, 112)
    combo.width = 140
    compare(combo.popup.width, 140)
}
```

该测试保护的生产缺陷是弹层再次被固定为窄控件宽度。

- [ ] **Step 2: 运行聚焦测试并确认 RED**

```bash
cmake --build /private/tmp/QtQuickComponents-combobox-build --parallel --target tst_qml_components
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build -R '^QmlComponents$' --output-on-failure
```

Expected: 创建组件时因 `popupMinimumWidth` 不存在失败，或弹层宽度仍为 76。

- [ ] **Step 3: 实现最小 API**

在根对象增加：

```qml
property real popupMinimumWidth: root.width
```

在内部 popup 改为：

```qml
width: Math.max(root.width, root.popupMinimumWidth)
```

- [ ] **Step 4: 重新构建并确认 GREEN**

```bash
cmake --build /private/tmp/QtQuickComponents-combobox-build --parallel --target tst_qml_components
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build -R '^QmlComponents$' --output-on-failure
```

Expected: `QmlComponents` 通过。

### Task 4: 验证并准备 QtQuickComponents 上游提交

**Files:**
- Modify: `/private/tmp/QtQuickComponents-combobox-fix/src/controls/ComboBox.qml`
- Modify: `/private/tmp/QtQuickComponents-combobox-fix/tests/qml/tst_controls.qml`

**Interfaces:**
- Produces: 一个只包含 ComboBox 修复及测试的提交哈希。

- [ ] **Step 1: 运行组件库聚焦与全量验证**

```bash
cmake --build /private/tmp/QtQuickComponents-combobox-build --parallel
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build -R '^(QmlComponents|QmlLint|QmlApiSurface)$' --output-on-failure
ctest --test-dir /private/tmp/QtQuickComponents-combobox-build --output-on-failure
git -C /private/tmp/QtQuickComponents-combobox-fix diff --check
```

- [ ] **Step 2: 审查变更范围并提交**

```bash
git -C /private/tmp/QtQuickComponents-combobox-fix diff -- src/controls/ComboBox.qml tests/qml/tst_controls.qml
git -C /private/tmp/QtQuickComponents-combobox-fix add src/controls/ComboBox.qml tests/qml/tst_controls.qml
git -C /private/tmp/QtQuickComponents-combobox-fix commit -m "[功能修改] 修复下拉框字体与弹层宽度"
```

- [ ] **Step 3: 停止并请求推送授权**

报告提交哈希、测试结果和远端目标。未取得明确授权前不执行 `git push`。

- [ ] **Step 4: 获得授权后推送组件分支**

```bash
git -C /private/tmp/QtQuickComponents-combobox-fix push -u origin fix/combobox-font-popup-width
```

确认远端可通过提交哈希读取后再进入 Task 5。

### Task 5: 更新 PluginBased 依赖和倍率选择器配置

**Files:**
- Modify: `/Users/zs/Downloads/PluginBased/CMakeLists.txt`
- Modify: `/Users/zs/Downloads/PluginBased/plugins/PlayPlugin/qml/ControlBar.qml`
- Modify: `/Users/zs/Downloads/PluginBased/tests/playplugin_regression_checks.py`

**Interfaces:**
- Consumes: 远端可达的 QtQuickComponents 修复提交和 `popupMinimumWidth: real`。
- Produces: PluginBased 对新组件提交的固定依赖及倍率弹层配置。

- [ ] **Step 1: 先更新宿主回归期望并确认 RED**

将倍率弹层契约从直接 `popup.width` 改为：

```python
require("popupMinimumWidth: 112" in control_bar_qml
        and "popup.width:" not in control_bar_qml,
        "Playback-rate selector should configure the public popup width API")
```

运行：

```bash
python3 tests/playplugin_regression_checks.py
```

Expected: 当前 `ControlBar.qml` 仍直接覆盖 `popup.width`，因此失败。

- [ ] **Step 2: 使用组件公共 API**

在 `ControlBar.qml` 中把：

```qml
popup.width: Math.max(rateSelector.width, 112)
```

替换为：

```qml
popupMinimumWidth: 112
```

保留 `Layout.preferredWidth: 76` 和 `Layout.minimumWidth: 76`。

- [ ] **Step 3: 更新 FetchContent pin**

将根 `CMakeLists.txt` 的 QtQuickComponents `GIT_TAG` 替换为 Task 4 产生并已推送的完整 40 位提交哈希。

- [ ] **Step 4: 运行宿主聚焦验证**

```bash
python3 tests/playplugin_regression_checks.py
cmake --preset debug
cmake --build --preset debug --target PlayPlugin PluginBasedApp --parallel
ctest --preset debug -R 'playplugin|Qml' --output-on-failure
```

Expected: 所有命令退出 0。

### Task 6: 完整验证和实际界面验收

**Files:**
- Verify only: `/Users/zs/Downloads/PluginBased`

**Interfaces:**
- Consumes: PluginBased 完整工作树和远端可达组件 pin。
- Produces: 自动化与视觉验收证据。

- [ ] **Step 1: 完整构建和测试**

```bash
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
git diff --check
```

- [ ] **Step 2: 启动应用进行视觉验收**

```bash
./build/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp
```

打开 PlayPlugin 和倍率菜单，确认：

- 收起值（包括 `2.0x`）完整显示。
- 六个菜单项均完整显示，无省略号。
- 普通项和选中项字符间距一致、紧凑。
- 菜单宽度不挤压控制栏中其他按钮。

- [ ] **Step 3: 审查最终差异**

```bash
git status --short
git diff -- CMakeLists.txt plugins/PlayPlugin/qml/ControlBar.qml tests/playplugin_regression_checks.py
```

确认只新增依赖 pin、组件公共 API 使用和对应测试契约，不覆盖其他未提交改动。

## Completion Definition

- QtQuickComponents 的修复提交远端可达，组件构建及全量 CTest 通过。
- PluginBased 固定到该提交，完整构建及全量 CTest 通过。
- 实际倍率菜单六项显示完整且字距正常。
- 未修改生成目录，未引入本地专用依赖哈希，未混入无关重构。
