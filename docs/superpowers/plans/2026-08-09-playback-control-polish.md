# Playback Control Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完整显示所有现有倍速标签，并把每次前进操作从 10 秒调整为 3 秒。

**Architecture:** 修改限于 PlayPlugin 的 QML 控制栏、QML 到 `PlayerEngine::seekBy` 的参数映射及插件翻译。现有 `PlayerEngine`、`PlaybackSeekState`、seek generation 和 SDK 播放链保持不变。

**Tech Stack:** Qt 6.8.3、QML、Python 3 回归检查、CMake/Ninja、CTest。

## Global Constraints

- 保持倍速标签为 `0.5x / 0.75x / 1.0x / 1.25x / 1.5x / 2.0x`。
- 保持当前控制栏视觉样式，不替换共享 `ComboBox` 皮肤。
- 不修改 C++ API、ABI、线程亲和性、所有权或 seek 状态机。
- 不编辑 `build/`、`build-release/`、`dist/` 等生成目录。
- 当前工作区已有未提交功能；未经用户明确要求，不创建 Git 提交。

---

### Task 1: 防止倍速标签被截断

**Files:**
- Modify: `tests/playplugin_regression_checks.py:103-110`
- Modify: `plugins/PlayPlugin/qml/ControlBar.qml:141-167`

**Interfaces:**
- Consumes: QuickUI `ComboBox` 已有的 `leftPadding: 10` 和 `rightPadding: 28`。
- Produces: 最小宽度为 76 px、文本层不再重复占用左右内边距的 `rateSelector`。

- [ ] **Step 1: 写入失败的布局回归检查**

在现有倍速预设断言后加入：

```python
    require("Layout.minimumWidth: 76" in control_bar_qml
            and "leftPadding: 0" in control_bar_qml
            and "rightPadding: 0" in control_bar_qml,
            "Playback-rate selector should preserve enough width for its complete label")
```

- [ ] **Step 2: 运行检查并确认因旧布局失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，消息为 `Playback-rate selector should preserve enough width for its complete label`。

- [ ] **Step 3: 实现最小布局修复**

在 `rateSelector` 中保留现有首选宽度并补充最小宽度：

```qml
                Layout.preferredWidth: 76
                Layout.minimumWidth: 76
```

把自定义文本层改为不重复增加内边距：

```qml
                contentItem: Text {
                    leftPadding: 0
                    rightPadding: 0
                    text: rateSelector.displayText
```

- [ ] **Step 4: 运行回归检查并确认通过**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS，退出码 0。

### Task 2: 将前进间隔调整为 3 秒

**Files:**
- Modify: `tests/playplugin_regression_checks.py:98-102`
- Modify: `plugins/PlayPlugin/qml/ControlBar.qml:130-136`
- Modify: `plugins/PlayPlugin/qml/PlayerView.qml:134`
- Modify: `plugins/PlayPlugin/translations/PlayPlugin_zh_CN.ts:99-102`

**Interfaces:**
- Consumes: `ControlBar.forwardRequested()` 和 `PlayerEngine::seekBy(qint64 deltaMs)`。
- Produces: `forwardRequested()` 到 `seekBy(3000)` 的 QML 映射，以及匹配的中英文提示。

- [ ] **Step 1: 把回归预期改为 3 秒并覆盖翻译**

在 `main()` 的读取区加入：

```python
    playplugin_translation = read("plugins/PlayPlugin/translations/PlayPlugin_zh_CN.ts")
```

用以下断言替换现有 10 秒断言：

```python
    require("forwardRequested" in control_bar_qml
            and 'qsTr("Forward 3 seconds")' in control_bar_qml,
            "ControlBar should expose the three-second forward command")
    require("onForwardRequested:   engine.seekBy(3000)" in read("plugins/PlayPlugin/qml/PlayerView.qml"),
            "PlayerView should map the forward command to a three-second relative seek")
    require("<source>Forward 3 seconds</source>" in playplugin_translation
            and "<translation>快进 3 秒</translation>" in playplugin_translation,
            "PlayPlugin should translate the three-second forward tooltip")
```

- [ ] **Step 2: 运行检查并确认因旧 10 秒行为失败**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: FAIL，消息为 `ControlBar should expose the three-second forward command`。

- [ ] **Step 3: 实现 3 秒映射和提示**

在 `ControlBar.qml` 中改为：

```qml
                tooltip: qsTr("Forward 3 seconds")
```

在 `PlayerView.qml` 中改为：

```qml
        onForwardRequested:   engine.seekBy(3000)
```

在 `PlayPlugin_zh_CN.ts` 中改为：

```xml
    <message>
        <source>Forward 3 seconds</source>
        <translation>快进 3 秒</translation>
    </message>
```

- [ ] **Step 4: 运行回归检查并确认通过**

Run: `python3 tests/playplugin_regression_checks.py`

Expected: PASS，退出码 0。

### Task 3: 聚焦验证和 Debug 全量验证

**Files:**
- Verify only: `plugins/PlayPlugin/qml/ControlBar.qml`
- Verify only: `plugins/PlayPlugin/qml/PlayerView.qml`
- Verify only: `plugins/PlayPlugin/translations/PlayPlugin_zh_CN.ts`
- Verify only: `tests/playplugin_regression_checks.py`

**Interfaces:**
- Consumes: Task 1 的布局约束和 Task 2 的 3000 ms 映射。
- Produces: 新鲜的编译、测试与差异检查证据。

- [ ] **Step 1: 构建最小受影响目标**

Run: `cmake --build --preset debug --target PlayPluginPlaybackSeekTest PlayPlugin PluginBasedApp --parallel`

Expected: 退出码 0，无编译错误。

- [ ] **Step 2: 运行聚焦测试**

Run: `ctest --test-dir build --output-on-failure -R 'playplugin_(regression_checks|playback_seek|pending_seek_requests|sdk_playback_adapter)'`

Expected: 匹配到的 PlayPlugin 测试全部通过，0 项失败。

- [ ] **Step 3: 运行 Debug 全量构建与测试**

Run: `cmake --build --preset debug --parallel`

Expected: 退出码 0。

Run: `ctest --preset debug`

Expected: 全部测试通过，0 项失败。若受限沙箱触发 Qt ARM64 `sysctl`/NEON 误报，记录原始输出，并在普通终端补跑对应测试。

- [ ] **Step 4: 检查差异质量和范围**

Run: `git diff --check`

Expected: 退出码 0。

Run: `git diff -- plugins/PlayPlugin/qml/ControlBar.qml plugins/PlayPlugin/qml/PlayerView.qml plugins/PlayPlugin/translations/PlayPlugin_zh_CN.ts tests/playplugin_regression_checks.py`

Expected: 仅包含既有前进功能、倍速完整显示、3 秒映射及对应测试/翻译变化，无无关格式化。

- [ ] **Step 5: 人工 UI 验证（需可见桌面与真实媒体）**

Run: `./build/app/PluginBasedApp.app/Contents/MacOS/PluginBasedApp`

Expected: 六个倍速标签在收起状态和下拉列表中均完整显示；普通、暂停、倍速和接近 EOF 场景下，单击/连续点击每次累计前进 3 秒且不越过媒体末尾。

若当前执行环境无法观察 GUI，明确把该项报告为未验证，不以自动化检查替代人工结论。
