# 视频帧对象池设计

## 1. 背景

当前视频帧链路为：

```text
FFmpeg decoder
    -> AVFramePtr
    -> VideoFrameProcessor
    -> media_sdk::VideoFrame
    -> PlaybackSession / RuntimeFrameQueue
    -> QtRhiVideoPresenter
    -> VideoFrameData
    -> FFmpegSurface / Qt Scene Graph
```

`media_sdk::VideoFrame` 是轻量值对象，像素平面由 `VideoFrameDesc::storage` 中的
`std::shared_ptr<void>` 保活。当前 `VideoFrameProcessor` 将 `AVFramePtr` 转成
`std::shared_ptr<AVFrame>`，最后一个引用释放时执行 `av_frame_free()`。

当前存在两类可优化的分配：

1. SDK 自己创建的 CPU 输出帧，例如不受渲染器支持的格式经 `sws_scale` 转换时，
   每帧都会调用 `av_frame_alloc()` 和 `av_frame_get_buffer()`。
2. `QtRhiVideoPresenter` 为跨 Qt Scene Graph 生命周期保存帧，会调用
   `av_frame_clone()`。该操作通常不复制像素，但会创建新的 `AVFrame` 及增加底层
   `AVBufferRef` 引用。

对 FFmpeg 软件解码器直接输出的受支持格式，像素缓冲由解码器管理。仅在 SDK
外层增加一个对象池，无法要求解码器把像素直接写进池中；若强制复制到池，会引入
整帧内存拷贝，不应作为默认实现。

## 2. 目标

- 参考 VLC `picture_t` 引用计数和 `picture_pool_t` 回收复用的生命周期模型。
- 复用 CPU 视频帧的 `AVFrame` 头和像素缓冲，降低稳定播放期间的分配与释放频率。
- 通过共享引用覆盖 decode、runtime、presenter 和 Qt Scene Graph 的跨线程生命周期。
- 最后一个引用释放后自动归还池，无需调用方显式 `release()`。
- seek、flush、分辨率切换、播放器关闭时不产生悬空引用或跨代复用错误。
- 池耗尽时保持播放链路可前进，避免解码线程永久等待。
- 不改变现有 `media_sdk::VideoFrame` 公共接口和 PlayPlugin 的呈现接口。

## 3. 非目标

- 第一阶段不池化音频帧。
- 第一阶段不池化 VideoToolbox、D3D11、VAAPI 等原生硬件帧。
- 第一阶段不实现通用的公开 SDK 对象池 API。
- 不把 PTS、session、generation、seek 状态或队列状态放入池对象。
- 不为了命中对象池而复制本来可零拷贝传递的解码输出帧。
- 不在第一阶段替换 FFmpeg 解码器的 `get_buffer2`；该能力必须经过独立基准和兼容性验证。

## 4. VLC 设计中采用与不采用的部分

采用以下原则：

- picture 是可引用的帧资源，最后一个引用释放后由回调归还 pool。
- pool 持有一组可复用 picture，借出和归还可以发生在不同线程。
- picture 的像素资源与单次播放帧的时间语义分离。
- pool 容量有限，生命周期和 shutdown 行为明确。

不直接照搬以下接口：

- 不公开 C 风格的 `picture_Hold()` / `picture_Release()`；C++ 调用方使用 RAII 引用。
- 不默认采用无限期阻塞的 `picture_pool_Wait()` 语义。当前 runtime 队列已经负责背压，
  pool 再永久阻塞会增加 seek/stop 死锁风险。
- 不让 PlayPlugin 或公共 SDK 调用方感知 pool 指针。
- 不把原生 GPU 资源和 CPU 平面放进同一个池实现。

参考实现：

- [VLC picture 接口](https://code.videolan.org/videolan/vlc/-/blob/master/include/vlc_picture.h)
- [VLC picture pool 接口](https://code.videolan.org/videolan/vlc/-/blob/master/include/vlc_picture_pool.h)
- [VLC picture pool 实现](https://code.videolan.org/videolan/vlc/-/blob/master/src/misc/picture_pool.c)

## 5. 总体方案

第一版新增 SDK 内部 `CpuVideoPicturePool`，池的借出类型为带自定义回收器的
`std::shared_ptr<AVFrame>`：

```cpp
using VideoPictureRef = std::shared_ptr<AVFrame>;

struct VideoPictureKey {
    int width = 0;
    int height = 0;
    AVPixelFormat pixelFormat = AV_PIX_FMT_NONE;
    int alignment = 32;
};

class CpuVideoPicturePool {
public:
    explicit CpuVideoPicturePool(VideoPicturePoolConfig config);
    ~CpuVideoPicturePool();

    [[nodiscard]] VideoPictureRef acquire(const VideoPictureKey& key);
    void close();
    [[nodiscard]] VideoPicturePoolStats stats() const;
};
```

`VideoPictureRef` 可直接转换为当前 `VideoFrameDesc::storage` 所需的
`std::shared_ptr<void>`。PlayPlugin 再通过 `std::static_pointer_cast<AVFrame>()`
获得同一个共享控制块，因此公共 `VideoFrame` 类型不需要变化。

### 5.1 所有权模型

```text
DecodeWorker owns VideoFrameProcessor
VideoFrameProcessor owns CpuVideoPicturePool facade
CpuVideoPicturePool facade owns shared PoolState

acquire()
  PoolState -> transfers AVFrame slot -> VideoPictureRef

VideoPictureRef copies
  VideoFrame -> runtime queue -> presenter -> FFmpegSurface / scene graph

last VideoPictureRef release
  custom deleter -> PoolState::recycle(slot)
```

自定义回收器持有 `std::shared_ptr<PoolState>`，而不是裸 `CpuVideoPicturePool*`。
因此 facade 先销毁时，在途帧仍可安全释放。`close()` 后：

- 立即释放空闲槽位；
- 新的 `acquire()` 返回空引用；
- 在途槽位归还时直接释放，不再进入 free list；
- 最后一个在途引用释放后 `PoolState` 自然销毁。

### 5.2 帧语义与像素存储分离

池槽位只保存可复用资源：

- `AVFrame` 头；
- `AVBufferRef` 持有的 CPU 像素内存；
- 固定的宽、高、像素格式、对齐信息。

每次借出前必须重置或覆盖单帧元数据：

- `pts`、`pkt_dts`、`duration`；
- color range、color space、primaries、transfer characteristic；
- sample aspect ratio、chroma location；
- crop、flags 及本实现写入的其他标量字段。

不能在回收时直接调用 `av_frame_unref()`，因为它会同时释放待复用的像素
`AVBufferRef`。第一版只允许池帧承载由 SDK 明确写入的标量元数据，不复制 side data、
metadata dictionary、opaque/private ref。若后续需要这些属性，应增加受测的清理函数，
而不是依赖遗漏字段的手工重置。

### 5.3 格式匹配与重配置

池使用单一 active key，而不是无限增长的多格式 bucket：

```text
(width, height, pixelFormat, alignment)
```

当 key 改变时：

1. 增加 pool format epoch；
2. 清空不兼容的空闲槽位；
3. 新 key 的 acquire 创建或复用新槽位；
4. 旧 epoch 的在途槽位归还时直接释放。

format epoch 只处理资源兼容性，不等同于播放 generation。seek 不改变图像格式，
因此不需要清空池；旧 generation 是否可进入队列仍由现有 session/runtime 规则判断。

### 5.4 容量与分配策略

建议默认配置：

```text
capacity: 12
initialRetained: 3
alignment: 32
exhaustionPolicy: AllocateTransient
```

容量应覆盖 runtime video queue、presenter pending、scene graph 在途帧、seek tail 和少量
调度余量。第一版不在媒体打开时一次性分配 12 个 4K 帧，避免启动时内存峰值；首次
识别 active key 后最多预热 3 个槽位，之后按高水位懒增长到 capacity。

当所有池槽位都在途时，`acquire()` 创建一个不回池的临时 `AVFrame` 并记录 miss。
该策略不会严格限制瞬时内存，但能保证 decode、seek 和 shutdown 不因 pool 等待互锁。
是否改成有超时的等待策略，应由运行数据证明有必要后再决定。

池容量只限制池拥有的槽位，不替代 runtime 队列容量和 presenter 背压。

### 5.5 线程模型

- `acquire()` 通常发生在 SDK decode worker。
- 最后一个引用可能在 runtime video thread、Qt object thread 或 Qt Scene Graph thread 释放。
- `PoolState` 的 free list、active key、epoch、closed 和统计数据受同一 mutex 保护。
- 不在持锁期间调用 `av_frame_get_buffer()`、`av_frame_free()` 或其他可能耗时的 FFmpeg API。
- 并发创建槽位前先在锁内登记 allocation reservation，完成后再提交或回滚，保证
  `retained + reserved <= capacity`。
- 回收路径不触发 Qt signal、日志回调或用户代码。
- `close()` 幂等，不等待在途帧归还。

## 6. 集成点

### 6.1 VideoFrameProcessor

第一阶段将池接入 SDK 自己拥有输出缓冲分配权的路径：

- `normalizeVideoFrame()` 从池获取 YUV420P 输出帧，再由 `sws_scale()` 写入；
- 后续评估硬件到 CPU transfer 是否能安全写入预分配目标帧，再决定是否接入；
- 已由软件解码器输出且渲染器支持的帧继续零拷贝传递，不做额外复制。

当前 `VideoFrameProcessor::reset()` 在 media close 时调用，而同一个 processor 之后仍可能
服务下一次 open。实现时 `reset()` 必须关闭旧 `PoolState` 并创建可供下一媒体使用的新
pool state，不能把 processor 永久置为 closed。旧媒体的在途引用继续持有旧 state，
归还时直接释放。

`createVideoFrame()` 接受 `VideoPictureRef` 或普通 `AVFramePtr`，统一生成
`VideoFrame`。普通解码帧继续使用当前 shared deleter；池帧使用 pool deleter。

### 6.2 PlayPlugin

`QtRhiVideoPresenter::makeSurfaceFrame()` 不再调用 `av_frame_clone()`，而是把
`VideoFrame::storage()` 中的 `std::shared_ptr<AVFrame>` 直接交给 `VideoFrameData`。

`VideoFrameData::frame` 从 `AVFramePtr` 改为 `std::shared_ptr<AVFrame>`。渲染代码只使用
`frame.get()`，不应需要行为变化。这样 SDK 和 Qt Scene Graph 真正共享同一引用计数；
只有 surface 释放帧后，池槽位才可能归还。

该修改同时适用于普通 CPU 帧和 native `AVFrame` 容器，但 native 帧本身不进入 CPU pool。

### 6.3 Decoder direct rendering（后续阶段）

若基准显示主要分配仍来自软件解码器，可评估通过 `AVCodecContext::get_buffer2`
把池缓冲交给解码器。该阶段必须单独设计并满足：

- 遵守 `avcodec_align_dimensions2()` 和每平面 linesize 对齐；
- 通过 `AVBufferRef` free callback 归还槽位；
- 支持 frame-threaded decoder 的并发回调；
- 不干扰硬件解码器自身的 `get_buffer2`；
- codec reopen、flush 和 close 时不等待旧缓冲；
- 对不兼容 codec 自动回退 FFmpeg 默认分配器；
- 证明收益高于回调、锁和兼容性成本。

在完成此阶段前，不能宣称“所有解码帧均来自 SDK picture pool”。

## 7. 状态与诊断

增加内部统计：

```cpp
struct VideoPicturePoolStats {
    std::uint64_t acquireCount = 0;
    std::uint64_t reuseCount = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t recycleCount = 0;
    std::uint64_t transientAllocationCount = 0;
    std::uint64_t incompatibleReturnCount = 0;
    std::uint64_t highWatermark = 0;
    std::uint64_t retainedCount = 0;
    std::uint64_t inFlightCount = 0;
};
```

这些数据并入现有 decode performance report，PlayPlugin 只记录，不据此改变播放策略。
report 中的解码、转换和 push 计数按报告周期重置；pool 的累计计数和
`retainedCount`/`inFlightCount` 在生成 report 时从 pool 重新采样。`DecodeFrameMetadata`
不携带 pool 快照，避免音频帧和无需转换的视频帧在逐帧热路径上获取 pool 锁。

`Player::diagnostics()` 按需读取当前 pool 状态，`PlaybackSession` 再将其与 runtime
队列/呈现诊断合并。因此帧在 runtime 或 Qt Scene Graph 线程归还后，下一次查询可以直接
观察到新的 gauge，不依赖后续视频帧入队。

需要区分：

- `reuseCount`：从 free list 借出已有槽位；
- `allocationCount`：创建可进入池的槽位；
- `transientAllocationCount`：池满时创建且释放后不回池的槽位。

## 8. 异常与降级

- 非法 key：返回空引用，由 `VideoFrameProcessor` 生成明确的 decode error。
- `av_frame_alloc()` 或 `av_frame_get_buffer()` 失败：返回空引用，不发布半初始化帧。
- pool 已关闭：返回空引用；正常 shutdown 路径不得继续处理新帧。
- 池满：临时分配；临时分配失败才中止当前帧处理。
- 分辨率/格式变化：旧槽位不复用到新 key。
- seek/flush：不清空兼容空闲槽位，在途旧帧按既有 generation 规则自然淘汰。
- presenter clear：释放持有的共享帧，不同步等待 pool。

## 9. 测试策略

### 9.1 Pool 单元测试

- 首次 acquire 分配正确格式和可写缓冲。
- 最后一个引用释放后，同一 key 再次 acquire 命中同一槽位。
- 多个共享引用存在时不提前回池。
- 归还后单帧元数据已重置，像素缓冲仍可写。
- 不同 key 不复用旧槽位。
- pool 满时使用临时分配且不超过 retained capacity。
- `close()` 后在途引用释放安全，且不会重新进入 free list。
- facade 析构早于在途引用时无 use-after-free。
- 多线程 acquire/release 压力测试无重复借出和计数错误。

### 9.2 集成测试

- `VideoFrameProcessor` 格式转换连续处理多帧后出现 pool reuse。
- `VideoFrame` 的 plane 指针在所有共享引用释放前保持有效。
- PlayPlugin presenter 不 clone `AVFrame`，surface 清理后引用数正确下降。
- seek、stop、reopen 和 native-to-CPU fallback 不复用不兼容帧。
- 运行完整 CTest，确保 runtime queue、seek、fallback 和 EOF drain 行为不变。

### 9.3 性能验收

使用固定媒体分别覆盖：

- 软件解码、渲染器直接支持的 YUV420P/NV12；
- 需要 `sws_scale` 的输入格式；
- VideoToolbox native 路径与 native-to-CPU fallback；
- 1080p 和 4K。

记录稳定播放阶段每秒分配次数、pool hit rate、RSS、高水位、decode/present 延迟和丢帧数。
第一阶段的最低验收是转换路径在预热后不再为每帧分配像素缓冲，且无显著 decode 延迟
或 RSS 回归。软件解码直出路径若没有明显变化属于预期结果。

## 10. 风险与约束

- `std::shared_ptr<void>` 是当前 SDK 的类型擦除边界，PlayPlugin 对其实际为 `AVFrame`
  的假设已经存在。第一阶段保持该约定，但后续可考虑用受控的 storage accessor 降低误用风险。
- 若 PlayPlugin 继续 clone，池帧归还时底层 buffer 可能仍被 clone 引用，重新获取后
  `av_frame_make_writable()` 会触发复制，削弱池收益，因此消除 clone 是完整方案的一部分。
- 过大的 retained capacity 会显著增加 4K 内存占用，容量不能简单等于旧 core 配置中的 30。
- 手工保留像素 buffer 时必须严格管理帧属性；新增 metadata/side data 复制前需要扩展 reset 测试。
- `get_buffer2` 影响 codec、线程和硬件后端兼容性，应作为独立里程碑，不与基础池一次提交。

## 11. 决策结论

采用 VLC 风格的“引用归零自动回池”模型，但按当前 SDK 架构分阶段落地：

1. 建立内部 CPU picture pool 和可跨线程的 RAII 生命周期；
2. 池化 SDK 自己分配的转换输出帧；
3. PlayPlugin 共享同一 `AVFrame` 引用，移除 presenter clone；
4. 完成诊断、正确性和性能验证；
5. 只有性能数据证明必要时，再接入 decoder `get_buffer2`。

该方案保留现有零拷贝路径，先解决确定可控的分配点，同时为后续真正的 decoder direct
rendering 留出接口和验证边界。
