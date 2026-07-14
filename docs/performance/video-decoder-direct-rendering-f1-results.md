# get_buffer2 F1 实验结果

## 决策

**NO-GO：不启动 F2 正式实现。**

原因：

- hevc_4k60_realtime best improvement 0.34% is below 3.00%

## 对比

| Case | Runs | Wall default/prototype | Improvement | CPU default/prototype | Improvement | RSS delta | Plane allocation/acquire |
|---|---:|---:|---:|---:|---:|---:|---:|
| h264_1080p24 | 5 | 227.60/225.91 ms | +0.74% | 1439.25/1438.06 ms | +0.08% | +0.41% | 45/750 |
| hevc_4k60_realtime | 5 | 1837.21/1830.90 ms | +0.34% | 12712.41/12720.73 ms | -0.07% | +0.06% | 51/1830 |

## 解释

FFmpeg default `get_buffer2` 已使用内部 `AVBufferPool`。prototype 的 plane
allocation/acquire 比例证明自管池能够复用，但不能证明它比 default 私有池减少了
更多分配；default 没有公开对应计数。只有 wall/CPU/RSS 的成对结果可用于两者性能
决策。

default 和 prototype 使用同一进程模型、demux/decode 代码和 callback 层级。正式运行
按奇偶轮交替执行顺序；媒体来源、SHA-256 和目标帧数由 manifest 固定。prototype
底层 allocation 只统计成功创建的 plane buffer，FFmpeg default 私有池没有对应公开
计数，因此不对两者的 allocation 次数作伪对比。

## 环境

- Label: `f4977df`
- Platform: `macOS-15.6.1-arm64-arm-64bit`
- Runner SHA-256: `5f3faa6752237af4c36bd441cff2b3e49112780a7126dab0282a28e817874518`
- Manifest SHA-256: `b40e6ca0bc242d443d863f2d798ed510db8cb40aa2918c1df4e7b424a21fe732`
- Raw results: `benchmark_artifacts/get_buffer2-f1`
- Machine report: `docs/performance/video-decoder-direct-rendering-f1-results.json`
