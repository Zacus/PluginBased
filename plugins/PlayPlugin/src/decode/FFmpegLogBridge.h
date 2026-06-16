#pragma once

// 安装 PlayPlugin 使用的 FFmpeg 全局日志回调。
// 回调是进程级全局状态，不捕获解码器实例或 QObject 状态。

void installFFmpegLogBridge();
