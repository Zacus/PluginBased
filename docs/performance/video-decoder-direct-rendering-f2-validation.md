# get_buffer2 F2 生产路径验证

## 结论

F2 功能验收通过。该结论只证明正式 SDK 路径、回退开关和生命周期正确，不改变 F1
“未达到性能收益门槛”的 NO-GO 结论。

## 结果

| Case | Delivered frames | Pixel format | Callback/pooled | Fallback | Rebuild | Plane allocation/acquire |
|---|---:|---|---:|---:|---:|---:|
| H.264 1080p24 enabled | 24 | yuv420p | 34/34 | 0 | 1 | 45/102 |
| H.264 1080p24 disabled | 24 | yuv420p | 0/0 | 0 | 0 | 0/0 |
| HEVC Main10 4K60 enabled | 60 | yuv420p10 | 69/69 | 0 | 1 | 51/207 |
| ProRes 422 10-bit 4K120 enabled | 120 | yuv422p10 | 120/120 | 0 | 1 | 36/360 |

enabled case 的 callback 数可能大于已交付帧数，因为多线程 decoder 可在 benchmark 发出
stop 前解码额外 frame。三个 enabled case 均满足 callback 全部由正式池处理且零 fallback。

H.264 enabled/disabled 均交付 24 帧，视频 checksum 同为
`7071350510766113730`。disabled case 没有创建正式池。四个 case 在 stop 后的 decoder pool
snapshot 均归零。

## 方法

使用 Debug 构建的 `MediaSdkVideoBenchmark`，经 `media_sdk::Player -> Demuxer ->
StreamDecoder` 正式路径运行，software decode，最多持有 3 个输出 frame。原始 JSON 位于
`benchmark_artifacts/get_buffer2-f2/`，该目录按仓库规则不提交。

本轮是功能和生命周期验收，不是 Release 多轮性能对照；不得用单轮 wall/CPU 数据宣称
F2 带来性能改善。
