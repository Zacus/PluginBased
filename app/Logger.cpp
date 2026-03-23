#include "Logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

void Logger::init(const std::string& logDir, spdlog::level::level_enum level)
{
    // 确保日志目录存在
    std::filesystem::create_directories(logDir);

    // 多 sink：彩色控制台 + 滚动文件（每个文件最大 5MB，保留 3 个）
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(level);

    const std::string logFile = logDir + "/videoplayer.log";
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFile,
        5 * 1024 * 1024,   // 5 MB
        3                   // 保留3个滚动文件
    );
    fileSink->set_level(level);

    m_logger = std::make_shared<spdlog::logger>(
        "vp",
        spdlog::sinks_init_list{consoleSink, fileSink}
    );

    // 格式：[时间] [级别] [线程id] 消息
    m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    m_logger->set_level(level);
    m_logger->flush_on(spdlog::level::warn);  // warn 及以上立即刷盘

    spdlog::register_logger(m_logger);
    spdlog::set_default_logger(m_logger);

    m_logger->info("Logger initialized — log file: {}", logFile);
}

void Logger::shutdown()
{
    if (m_logger) {
        m_logger->flush();
    }
    spdlog::shutdown();
}
