#pragma once

#include <QSettings>
#include <QString>
#include <memory>
#include <spdlog/spdlog.h>

namespace PluginBased::App {

/**
 * @brief 应用配置管理（单例）
 *
 * 使用 INI 格式，基于 Qt6 QSettings。
 * 配置文件路径：<AppLocalDataLocation>/config/pluginbased.ini
 *
 * 示例 INI 内容：
 * ─────────────────────────────
 * [log]
 * level=debug          ; trace | debug | info | warn | error | critical | off
 * dir=logs             ; 相对路径（相对于 AppLocalDataLocation）或绝对路径
 * max_file_size_mb=5   ; 单个日志文件最大 MB
 * max_files=3          ; 滚动保留文件数
 * flush_on=warn        ; 达到此级别立即 flush
 *
 * [ui]
 * theme=dark           ; themes/<id>.json，内置支持 dark / light
 * ─────────────────────────────
 */
class AppConfig
{
public:
    static AppConfig& instance()
    {
        static AppConfig s;
        return s;
    }

    /**
     * @brief 加载（或创建）配置文件
     * @param configPath  INI 文件完整路径；若文件不存在则自动写入默认值
     */
    void load(const QString& configPath);

    // ── log 配置 ──────────────────────────────────────────
    spdlog::level::level_enum logLevel()     const { return m_logLevel;       }
    QString                   logDir()       const { return m_logDir;         }
    int                       logMaxFileMB() const { return m_logMaxFileMB;   }
    int                       logMaxFiles()  const { return m_logMaxFiles;    }
    spdlog::level::level_enum logFlushOn()   const { return m_logFlushOn;     }

    // ── ui 配置 ───────────────────────────────────────────
    QString themeName() const { return m_themeName; }
    void setThemeName(const QString& themeName);

    /** 将配置写回磁盘（手动调用或热重载后保存） */
    void save();

    /** 返回当前配置文件路径 */
    QString path() const { return m_path; }

private:
    AppConfig() = default;

    static spdlog::level::level_enum parseLevel(const QString& s,
                                                spdlog::level::level_enum fallback);
    static QString levelToString(spdlog::level::level_enum lv);

    void applyDefaults();

    QString                   m_path;
    std::unique_ptr<QSettings> m_settings;

    // ── 运行时缓存值（避免每次都走 QSettings 查询） ──────
    spdlog::level::level_enum m_logLevel     { spdlog::level::debug };
    QString                   m_logDir       { "logs" };
    int                       m_logMaxFileMB { 5 };
    int                       m_logMaxFiles  { 3 };
    spdlog::level::level_enum m_logFlushOn   { spdlog::level::warn };

    QString m_themeName { "dark" };
};

} // namespace PluginBased::App
