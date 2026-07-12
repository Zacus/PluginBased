# 视频帧对象池任务拆分

关联设计：[视频帧对象池设计](../design/video-picture-pool-design.md)

## 1. 实施原则

- 每个任务形成可独立审查的提交，不把 decoder `get_buffer2` 与基础池混在一起。
- 先完成生命周期和并发测试，再接入播放链路。
- 保持 `media_sdk::VideoFrame` 公共接口不变。
- 默认只池化 CPU 帧；native 硬件帧沿用现有 FFmpeg/CoreVideo 引用生命周期。
- 每个阶段先运行最小相关测试，阶段结束后运行完整 build 和 CTest。

## 2. 里程碑 A：基线与测量

### A1. 建立分配基线

范围：`sdk/media_core` 的测试和诊断，不改变池化行为。

工作项：

- 为 `VideoFrameProcessor` 当前转换路径记录帧分配次数或增加测试用 allocation hook。
- 确认软件直出、格式转换、硬件 transfer 三条路径各自由谁分配像素缓冲。
- 记录 `QtRhiVideoPresenter::makeSurfaceFrame()` 的 clone 次数。
- 准备至少一个需要格式转换的测试帧和一个直接支持的 YUV/NV12 测试帧。
- 记录 1080p/4K 样本的稳定播放 RSS、decode latency、drop count。

验收标准：

- 基线结果可重复，能够区分 AVFrame 头分配和像素 buffer 分配。
- 文档或测试输出明确说明第一阶段可优化的分配点。

建议提交：`[测试] 建立视频帧分配性能基线`

## 3. 里程碑 B：内部 CPU Picture Pool

### B1. 定义池类型和配置

新增建议文件：

```text
sdk/media_core/src/CpuVideoPicturePool.h
sdk/media_core/src/CpuVideoPicturePool.cpp
sdk/media_core/tests/tst_media_core_video_picture_pool.cpp
```

工作项：

- 定义 `VideoPictureKey`、`VideoPicturePoolConfig`、`VideoPicturePoolStats`。
- 定义 `VideoPictureRef = std::shared_ptr<AVFrame>`。
- `CpuVideoPicturePool` 不可复制，提供 `acquire()`、`close()`、`stats()`。
- 实现内部 `PoolState`，回收器只持有 `std::shared_ptr<PoolState>`。
- 给必须检查的 `acquire()` 返回值添加 `[[nodiscard]]`。
- 更新 `sdk/media_core/CMakeLists.txt`。

验收标准：

- 类型仅在 `media_sdk_core` 内部可见，不安装为公共头文件。
- 无裸 owning pointer，facade 析构后在途引用仍可安全释放。
- 非法 key 和 closed 状态有明确返回行为。

### B2. 实现分配、借出和自动回收

工作项：

- 为 active key 分配 `AVFrame` 并调用 `av_frame_get_buffer(..., 32)`。
- free list 借出时验证 key、epoch 和 `av_frame_is_writable()`。
- 最后一个共享引用释放时自动回收。
- 回收时重置单帧标量元数据，但保留像素 `AVBufferRef`。
- FFmpeg 分配/释放在 mutex 外完成，锁内只修改池状态和容器。
- 使用 allocation reservation 保证多个并发 acquire 不会突破 retained capacity。
- 维护 retained、in-flight 和 high-watermark 计数。

验收标准：

- 同 key 的第二次 acquire 可复用相同槽位和像素 data 指针。
- 未释放的槽位不会被再次借出。
- 回收后的帧可写，且 PTS/颜色等上次帧属性不会泄漏。

### B3. 实现容量、重配置和关闭

工作项：

- 实现 retained capacity 和 initial retained 策略。
- pool 满时创建 transient frame，不永久等待。
- key 变化时推进 format epoch 并释放不兼容空闲槽位。
- 旧 epoch 在途槽位归还时直接释放。
- `close()` 幂等，清理空闲槽位，不等待在途引用。
- pool 关闭后禁止新 acquire。

验收标准：

- retained 数量不超过配置 capacity。
- 分辨率切换后不会把旧 buffer 当作新格式使用。
- close、析构、在途释放的任意顺序均无崩溃和泄漏。

### B4. Pool 单元测试

测试项：

- allocate and reuse；
- multiple references delay recycle；
- metadata reset；
- incompatible key and epoch；
- capacity exhaustion and transient allocation；
- close with in-flight references；
- pool facade destroyed before reference；
- concurrent acquire/release stress。

验收命令：

```bash
cmake --build build --target MediaSdkCoreVideoPicturePoolTest --parallel
ctest --test-dir build -R media_sdk_core_video_picture_pool --output-on-failure
```

建议提交：`[功能新增] 增加CPU视频帧对象池`

## 4. 里程碑 C：接入 VideoFrameProcessor

### C1. 池化格式转换输出帧

范围：`VideoFrameProcessor` 的 `normalizeVideoFrame()`。

工作项：

- `VideoFrameProcessor` 持有 `CpuVideoPicturePool`。
- 转换目标 key 使用源宽高、`AV_PIX_FMT_YUV420P` 和 32 字节对齐。
- 从池获取 writable frame，设置本帧元数据，再执行 `sws_scale()`。
- `createVideoFrame()` 能接受 pool-backed shared frame，并保持 plane view 生命周期。
- 处理失败时引用自然释放，不把半初始化帧发布到 runtime。
- `reset()` 只重置 `SwsContext` 和播放期状态；普通 seek 不销毁兼容池。
- media close 时关闭旧 `PoolState` 并立即为下一次 open 建立新 state；旧媒体在途帧继续
  持有旧 state，归还时直接释放。

验收标准：

- 连续转换相同格式帧，预热后 `reuseCount` 增长。
- 输出像素格式、plane、stride、PTS 和颜色信息与改动前一致。
- 支持格式的软件解码帧仍直接透传，没有新增整帧复制。

### C2. 扩展 VideoFrameProcessor 测试

测试项：

- 连续转换帧复用 pool storage；
- 两个结果同时在途时不会复用同一槽位；
- 释放结果后槽位可复用；
- 分辨率变化不会复用旧槽位；
- 转换失败不污染下一帧；
- native frame 不进入 CPU pool。

验收命令：

```bash
cmake --build build --target MediaSdkCoreVideoFrameProcessorTest --parallel
ctest --test-dir build -R media_sdk_core_video_frame_processor --output-on-failure
```

建议提交：`[功能修改] 复用视频格式转换输出缓冲`

## 5. 里程碑 D：PlayPlugin 共享引用

### D1. 移除 presenter AVFrame clone

涉及文件：

```text
plugins/PlayPlugin/src/common/FFmpegUtils.h
plugins/PlayPlugin/src/playback/QtRhiVideoPresenter.cpp
plugins/PlayPlugin/src/video/*
```

工作项：

- 将 `VideoFrameData::frame` 改为 `std::shared_ptr<AVFrame>`。
- 增加接受 shared frame 的 `make_video_frame()` 重载或替换现有签名。
- `QtRhiVideoPresenter::makeSurfaceFrame()` 直接共享 SDK storage 控制块。
- 删除 `av_frame_clone()`，保持 native handle、full range、BT.709 推导不变。
- 检查所有渲染端仅借用 `frame.get()`，不调用 `av_frame_unref()` 或修改 frame。
- 明确 scene graph 对 `VideoFrameDataPtr` 的持有覆盖纹理上传/呈现周期。

验收标准：

- presenter 路径不再调用 `av_frame_clone()`。
- surface 持帧期间 SDK pool 不会提前回收该槽位。
- clear、连续 present 覆盖 pending frame、窗口销毁时均正确释放共享引用。

### D2. Presenter 生命周期测试

工作项：

- 增加 shared storage 在 presenter/surface 持有期间不析构的测试。
- 增加 clear 后最后一个引用释放的测试。
- 覆盖 CPU frame 和 VideoToolbox native frame。
- 若现有 Qt 测试难以观察引用释放，增加内部测试 storage deleter 计数器。

验收命令：

```bash
cmake --build build --parallel
ctest --test-dir build -R 'play|present|video' --output-on-failure
```

建议提交：`[功能修改] 共享SDK与渲染器视频帧引用`

## 6. 里程碑 E：诊断与端到端验证

### E1. 接入性能统计

工作项：

- 将 pool acquire/reuse/allocation/transient/high-watermark 汇入 `DecodePerformanceStats`。
- `DecodePerformanceReport` 输出周期内计数和必要的累计高水位。
- `SdkPlaybackAdapter` 日志增加 pool 指标，不基于指标改变策略。
- 明确 report reset 时累计值和区间值的语义。
- pool gauge 通过 core 按需诊断读取，不放入逐帧 `DecodeFrameMetadata`；session 查询时与
  runtime 诊断合并。

验收标准：

- 日志可判断池是否命中、是否频繁耗尽、容量是否过大。
- 空闲播放或 audio-only 媒体不产生无意义 pool 报告。

### E2. Seek、fallback 和 shutdown 回归

测试项：

- seek generation 切换时旧帧仍可安全释放；
- seek tail frame 持有期间不提前回收；
- native-to-CPU fallback 重新配置 pool key；
- stop/close 不等待 Qt Scene Graph 归还帧；
- EOF drain 后帧全部归还或随 PoolState 安全释放；
- 重复 open/close 无 retained memory 持续增长。

### E3. 完整验证

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

手工验证：

- 软件解码 1080p/4K；
- VideoToolbox native；
- native presenter failure 后 CPU fallback；
- 连续 seek、暂停恢复、切换媒体、关闭窗口；
- 比较里程碑 A 的分配、RSS、延迟和丢帧基线。

验收标准：

- 转换路径预热后不再逐帧分配像素 buffer。
- 无 use-after-free、double recycle、死锁或 shutdown 超时。
- 无显著 decode latency、RSS 或丢帧回归。
- pool miss 持续偏高时先分析容量和持帧链路，不直接无限扩容。

建议提交：`[功能修改] 增加视频帧池诊断与回归验证`

## 7. 里程碑 F：Decoder Direct Rendering 可行性门禁

该里程碑不是基础对象池上线的必要条件。只有里程碑 E 的数据表明软件解码器像素分配
仍是主要瓶颈时才启动。

2026-07-12 的 H.264 1080p/HEVC 4K Release benchmark 已完成数据门禁，结果为
`CLOSED`。在补充 allocator 归因并达到 CPU 5% 或 allocation 15% 之前，不启动 F1。

### F1. 技术验证

工作项：

- 调研目标 FFmpeg 版本中 `get_buffer2`、frame threading 和 codec alignment 契约。
- 做独立原型，通过自定义 `AVBufferRef` callback 回收像素槽位。
- 验证 H.264、HEVC、VP9/AV1 中项目实际支持的 codec。
- 明确硬件 decoder 保持默认回调的判断条件。
- 对比默认 FFmpeg allocator 与自定义 pool 的吞吐、锁竞争和内存。

通过门槛：

- 不增加整帧复制；
- 多线程 decode、flush、reopen、stop 全部通过压力测试；
- 目标媒体有可重复的分配或延迟收益；
- 不兼容 codec 可无行为差异地回退默认 allocator。

### F2. 正式实现

F1 通过后再编写单独设计补充和任务拆分，不直接在本任务中展开实现。

建议提交：`[技术验证] 评估解码器直连视频帧池`

## 8. 推荐执行顺序

```text
A1
 -> B1 -> B2 -> B3 -> B4
 -> C1 -> C2
 -> D1 -> D2
 -> E1 -> E2 -> E3
 -> [数据门禁] -> F1 -> F2
```

基础功能的最小可合并范围为 B、C、D、E。A 用于证明收益，F 由数据决定是否继续。

## 9. 完成定义

- 设计中的所有权、线程和 shutdown 契约均有对应测试。
- `media_sdk::VideoFrame` 公共 API 无破坏性变化。
- PlayPlugin 不再为 presenter 生命周期 clone `AVFrame`。
- SDK 自分配的 CPU 转换帧能够引用归零后自动回池。
- pool 容量有限，耗尽时可前进，关闭时不等待在途帧。
- 完整 build 和 CTest 通过。
- 性能对比说明收益范围，并明确软件 decoder direct output 是否仍需 F 阶段。
