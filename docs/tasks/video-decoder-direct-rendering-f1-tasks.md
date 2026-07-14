# Decoder Direct Rendering F1 任务拆分

关联设计：[Decoder Direct Rendering F1 设计](../design/video-decoder-direct-rendering-f1.md)

状态：F1.1 至 F1.4 已完成。最终结论为 **NO-GO**，F2 不启动。

## 1. 约束

- F1 是 `tools/video_benchmark/` 下的独立实验，不进入 SDK/PlayPlugin 默认路径。
- 只使用 FFmpeg 8.0.1 公开 API，不访问 `AVCodecContext::internal`。
- default allocator 始终作为 fallback 和对照组。
- 每个任务单独提交；实验失败也记录结果，不通过扩大范围掩盖问题。

## 2. F1.1：冻结设计和退出门槛

- 范围：设计、所有权、线程、fallback、指标和 GO/NO-GO 条件。
- 验收：明确 FFmpeg 默认实现已经使用 `AVBufferPool`，不再以“消除逐帧 malloc/free”作为
  未经验证的收益假设。
- 建议提交：`[技术验证] 设计解码器直连帧池实验`

## 3. F1.2：实现独立 get_buffer2 原型

- 范围：新增 experimental decoder buffer pool 和独立 runner/CMake target。
- 工作：
  - default/prototype 两种 allocator mode；
  - 按 format/width/height/alignment 创建每平面 `AVBufferPool`；
  - 自定义底层 allocation callback 和统计；
  - DR1、hardware、无效布局、获取失败全部 fallback；
  - 输出 checksum、资源使用和 allocator diagnostics JSON。
- 排除：不修改 `PlayerConfig`、`Demuxer`、`DecodeWorker` 或 PlayPlugin。
- 验收：固定媒体可在两种 mode 完成相同目标帧数，prototype callback 实际命中。
- 建议提交：`[技术验证] 实现隔离的get_buffer2帧池原型`

## 4. F1.3：生命周期与兼容性测试

- 范围：prototype pool 单元测试和 FFmpeg integration test。
- 工作：
  - alignment、plane coverage、buffer allocation/reuse；
  - format epoch 切换时新旧 frame 同时在途；
  - close/uninit 后延迟释放；
  - pool 获取失败、hardware context、非 DR1 fallback；
  - frame-threading decode、flush、重复创建销毁。
- 验收：无重复 buffer、UAF、double free、死锁或未回退的部分 frame。
- 建议提交：`[组件测试] 验证get_buffer2原型生命周期`

## 5. F1.4：真实媒体对比和决策

- 范围：H.264 1080p24、HEVC Main10 4K60，Release，default/prototype 各 1 次预热和
  5 次正式运行。
- 指标：帧数、checksum、wall/CPU/RSS、底层 plane allocation、callback/fallback。
- GO：功能全部通过，wall/CPU 至少改善 3% 或分配/延迟抖动有显著收益，RSS 增幅不超过
  5%，无新增丢帧。
- NO-GO：收益不足、依赖私有实现或 fallback/lifetime 无法稳定。
- 产物：机器 JSON、结果文档和是否启动 F2 的明确结论。
- 建议提交：`[测试] 完成get_buffer2原型性能决策`

## 6. 集成验证

```bash
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

原型完成后额外构建 Release benchmark target，并使用 manifest 固定媒体执行对照。F1 不
改变应用运行时行为，因此不以手工 UI 播放代替原型的 checksum、生命周期和基准验证。

## 7. 完成定义

- default/prototype 对照可重复运行；
- 所有 fallback、format change 和在途释放行为有测试；
- 原始数据和环境指纹可复核；
- GO/NO-GO 结论符合预先冻结的门槛；
- NO-GO 时不把实验开关加入公共 SDK。

实际结果满足正确性、fallback 和生命周期要求，但 HEVC 4K60 的最佳性能改善仅
`0.34%`，未达到 3% 门槛。实验代码保留在 benchmark tools 供结果复核，不进入正式 SDK
链路。
