#include "Logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

void Logger::init(const std::string&        logDir,
                  spdlog::level::level_enum level,
                  std::size_t               maxFileSizeBytes,
                  std::size_t               maxFiles,
                  spdlog::level::level_enum flushOn)
{
    // 确保日志目录存在
    std::filesystem::create_directories(logDir);

    // 多 sink：彩色控制台 + 滚动文件
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(level);

    const std::string logFile = logDir + "/pluginbased.log";
    auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFile,
        maxFileSizeBytes,
        maxFiles
    );
    fileSink->set_level(level);

    m_logger = std::make_shared<spdlog::logger>(
        "vp",
        spdlog::sinks_init_list{consoleSink, fileSink}
    );

    // 格式：[时间] [级别] [线程id] 消息
    m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    m_logger->set_level(level);
    m_logger->flush_on(flushOn);

    spdlog::register_logger(m_logger);
    spdlog::set_default_logger(m_logger);

    m_logger->info("Logger initialized — log file: {}", logFile);
    m_logger->info("Log level: {}  flush_on: {}  max_file: {} MB  max_files: {}",
                   spdlog::level::to_string_view(level).data(),
                   spdlog::level::to_string_view(flushOn).data(),
                   maxFileSizeBytes / (1024 * 1024),
                   maxFiles);
}

void Logger::setLevel(spdlog::level::level_enum level)
{
    if (!m_logger) return;

    m_logger->set_level(level);
    for (auto& sink : m_logger->sinks())
        sink->set_level(level);

    m_logger->info("Log level changed to: {}",
                   spdlog::level::to_string_view(level).data());
}

void Logger::shutdown()
{
    if (m_logger) {
        m_logger->flush();
    }
    spdlog::shutdown();
}
