#include "decode/FFmpegLogBridge.h"

// Adapts FFmpeg C log callbacks to the project logger.
// This keeps global callback wiring out of FFmpegDecoder's object lifecycle.

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
