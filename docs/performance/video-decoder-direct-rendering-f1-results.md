# get_buffer2 F1 实验结果

## 决策

**NO-GO：不启动 F2 正式实现。**

原因：

- hevc_4k60_realtime best improvement -0.29% is below 3.00%

## 对比

| Case | Runs | Throughput default/prototype | Wall improvement | CPU improvement | RSS delta | Plane allocation/acquire |
|---|---:|---:|---:|---:|---:|---:|
| h264_1080p24 | 5 | 1061.83/1056.84 fps | -0.47% | +0.53% | -0.26% | 45/750 |
| hevc_4k60_realtime | 5 | 324.21/322.58 fps | -0.50% | -0.29% | +0.02% | 51/1830 |
| prores_4k120_422p10_stress | 5 | 267.14/269.95 fps | +1.04% | -0.15% | +0.09% | 36/360 |

## 解释

FFmpeg default `get_buffer2` 已使用内部 `AVBufferPool`。prototype 的 plane
allocation/acquire 比例证明自管池能够复用，但不能证明它比 default 私有池减少了
更多分配；default 没有公开对应计数。只有 wall/CPU/RSS 的成对结果可用于两者性能
决策。

default 和 prototype 使用同一进程模型、demux/decode 代码和 callback 层级。正式运行
按奇偶轮交替执行顺序；媒体来源、SHA-256 和目标帧数由 manifest 固定。prototype
底层 allocation 只统计成功创建的 plane buffer，FFmpeg default 私有池没有对应公开
计数，因此不对两者的 allocation 次数作伪对比。

4K120 4:2:2 10-bit case 是确定性 synthetic ProRes throughput stress，不包含实时
PTS 节流、音频时钟、Qt Scene Graph 或 GPU texture upload；它不替代 HEVC 4K60
产品主 case。

## 环境

- Label: `7c7ebb8`
- Platform: `macOS-15.6.1-arm64-arm-64bit`
- Runner SHA-256: `6c6654b3f9755426085c6df6af4a0be75f121d142c5be67430166a05c69948b9`
- Manifest SHA-256: `622e0dc98dbde2bab9d764a2d48a0d772ba9ab3778e72730fca792e968fe1379`
- Raw results: `benchmark_artifacts/get_buffer2-f1`
- Machine report: `docs/performance/video-decoder-direct-rendering-f1-results.json`
