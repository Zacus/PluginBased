#include "decode/FFmpegLogBridge.h"

// 将 FFmpeg 的 C 日志回调适配到项目日志系统。
// 全局回调安装逻辑独立出来，避免进入 FFmpegDecoder 的对象生命周期。

#include "FFmpegUtils.h"
#include "Logger.h"

#include <cstdarg>
#include <cstdio>
#include <string>

void installFFmpegLogBridge()
{
    av_log_set_callback(
        [](void*, int level, const char* fmt, va_list args)
        {
            if (level > AV_LOG_WARNING)
                return;

            char buffer[1024];
            vsnprintf(buffer, sizeof(buffer), fmt, args);

            std::string message(buffer);
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.pop_back();
            if (message.empty())
                return;

            if (level <= AV_LOG_ERROR)
                LOG_ERROR("[FFmpeg] {}", message);
            else
                LOG_WARN("[FFmpeg] {}", message);
        });
}
