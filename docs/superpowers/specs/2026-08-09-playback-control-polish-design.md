# 播放控制栏显示与快进间隔调整设计

## 背景

`ControlBar.qml` 的倍速选择框固定为 76 px，并在 QuickUI `ComboBox` 已有左右内边距的
基础上，又给自定义 `contentItem` 添加了 10 px 和 22 px 的内边距。两层内边距叠加后，
倍速文本的可用空间过小，界面会把 `1.0x`、`1.25x` 等标签截断。

当前未提交的前进功能从 `PlayerView.qml` 调用 `PlayerEngine::seekBy(10000)`，产品反馈认为
10 秒跨度过大，需调整为 3 秒。

## 目标

- 完整显示现有 `0.5x / 0.75x / 1.0x / 1.25x / 1.5x / 2.0x` 标签。
- 保持倍速标签格式和控制栏现有视觉样式。
- 每次点击前进按钮相对当前位置前进 3 秒。
- 同步英文提示、中文翻译和回归检查。
- 保持连续点击、边界钳制和 seek generation 过滤行为不变。

## 非目标

- 不改变支持的播放倍率、倍速切换状态或 SDK 播放逻辑。
- 不重构 `PlaybackSeekState` 或 `PlayerEngine`。
- 不增加后退按钮或可配置的跳转间隔。

## 设计

在 `ControlBar.qml` 中移除倍速文本层的重复左右内边距，并给选择框保留明确的最小宽度，
使 `RowLayout` 不会把它压缩到无法显示最长标签。模型、字体、背景和下拉指示器保持不变。

前进事件链保持现有边界，只调整 QML 传入的增量：

```text
ControlBar.forwardRequested()
  -> PlayerView: engine.seekBy(3000)
  -> PlayerEngine / PlaybackSeekState（行为不变）
```

按钮提示由 `Forward 10 seconds` 改为 `Forward 3 seconds`，中文翻译同步改为“快进 3 秒”。
不新增 C++ API，因此不影响 ABI、对象所有权、线程亲和性或关闭顺序。

## 备选方案

- 仅增大选择框宽度：会掩盖重复内边距问题，并占用不必要的控制栏空间。
- 删除局部样式并完全采用共享 `ComboBox` 默认样式：能解决问题，但会带来超出需求的视觉变化。

## 验证

- 先更新 `playplugin_regression_checks.py`，使其要求 3 秒映射、3 秒提示和防压缩布局，确认旧实现失败。
- 修改 QML 与翻译后运行聚焦的 PlayPlugin 回归检查和 seek 测试。
- 构建 `PlayPlugin` 及应用，再运行 Debug CTest。
- 人工启动应用，确认所有倍速标签完整显示，并用真实媒体验证单击和连续点击均按 3 秒前进。

## 剩余风险

自动化回归检查只能验证声明和映射，最终字体渲染与布局仍需在应用窗口中人工观察。
