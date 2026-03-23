#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>
#include <memory>

// ── 便捷宏，携带文件/行信息 ──────────────────────────────────────────────
#define LOG_TRACE(...)    Logger::instance().trace(__VA_ARGS__)
#define LOG_DEBUG(...)    Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...)     Logger::instance().info(__VA_ARGS__)
#define LOG_WARN(...)     Logger::instance().warn(__VA_ARGS__)
#define LOG_ERROR(...)    Logger::instance().error(__VA_ARGS__)
#define LOG_CRITICAL(...) Logger::instance().critical(__VA_ARGS__)

class Logger
{
public:
    static Logger& instance();

    /**
     * @brief 初始化日志系统
     * @param logDir  日志目录（默认 ./logs）
     * @param level   最低输出级别（默认 debug）
     */
    void init(const std::string& logDir        = "logs",
              spdlog::level::level_enum level   = spdlog::level::debug,
              std::size_t maxFileSizeBytes       = 5 * 1024 * 1024,
              std::size_t maxFiles               = 3,
              spdlog::level::level_enum flushOn  = spdlog::level::warn);

    /**
     * @brief 运行时动态修改日志级别（热重载配置时调用，无需重启）
     * @param level  新的最低输出级别
     */
    void setLevel(spdlog::level::level_enum level);

    void shutdown();

    // 转发到 spdlog，保留 fmt 格式化
    template<typename... Args>
    void trace(spdlog::format_string_t<Args...> fmt, Args&&... args)
    { if (m_logger) m_logger->trace(fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void debug(spdlog::format_string_t<Args...> fmt, Args&&... args)
    { if (m_logger) m_logger->debug(fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void info(spdlog::format_string_t<Args...> fmt, Args&&... args)
    { if (m_logger) m_logger->info(fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void warn(spdlog::format_string_t<Args...> fmt, Args&&... args)
    { if (m_logger) m_logger->warn(fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void error(spdlog::format_string_t<Args...> fmt, Args&&... args)
    { if (m_logger) m_logger->error(fmt, std::forward<Args>(args)...); }

    template<typename... Args>
    void critical(spdlog::format_string_t<Args...> fmt, Args&&... args)
    { if (m_logger) m_logger->critical(fmt, std::forward<Args>(args)...); }

private:
    Logger() = default;
    std::shared_ptr<spdlog::logger> m_logger;
};
