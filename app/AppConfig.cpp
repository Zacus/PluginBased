#include "AppConfig.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

// ── 级别字符串映射 ──────────────────────────────────────────────────────────

static const struct {
    const char*               str;
    spdlog::level::level_enum lv;
} kLevelMap[] = {
    { "trace",    spdlog::level::trace    },
    { "debug",    spdlog::level::debug    },
    { "info",     spdlog::level::info     },
    { "warn",     spdlog::level::warn     },
    { "warning",  spdlog::level::warn     },  // 别名
    { "error",    spdlog::level::err      },
    { "err",      spdlog::level::err      },  // 别名
    { "critical", spdlog::level::critical },
    { "off",      spdlog::level::off      },
};

spdlog::level::level_enum AppConfig::parseLevel(const QString& s,
                                                 spdlog::level::level_enum fallback)
{
    const QByteArray lower = s.trimmed().toLower().toUtf8();
    for (const auto& m : kLevelMap) {
        if (lower == m.str)
            return m.lv;
    }
    return fallback;
}

QString AppConfig::levelToString(spdlog::level::level_enum lv)
{
    for (const auto& m : kLevelMap) {
        if (m.lv == lv)
            return QString::fromUtf8(m.str);
    }
    return QStringLiteral("debug");
}

// ── 核心逻辑 ────────────────────────────────────────────────────────────────

void AppConfig::applyDefaults()
{
    // 只有 key 不存在时才写入默认值（保留用户已有设置）
    m_settings->beginGroup(QStringLiteral("log"));

    if (!m_settings->contains(QStringLiteral("level")))
        m_settings->setValue(QStringLiteral("level"),    QStringLiteral("debug"));
    if (!m_settings->contains(QStringLiteral("dir")))
        m_settings->setValue(QStringLiteral("dir"),      QStringLiteral("logs"));
    if (!m_settings->contains(QStringLiteral("max_file_size_mb")))
        m_settings->setValue(QStringLiteral("max_file_size_mb"), 5);
    if (!m_settings->contains(QStringLiteral("max_files")))
        m_settings->setValue(QStringLiteral("max_files"),        3);
    if (!m_settings->contains(QStringLiteral("flush_on")))
        m_settings->setValue(QStringLiteral("flush_on"), QStringLiteral("warn"));

    m_settings->endGroup();

    m_settings->beginGroup(QStringLiteral("ui"));
    if (!m_settings->contains(QStringLiteral("theme")))
        m_settings->setValue(QStringLiteral("theme"), QStringLiteral("dark"));
    m_settings->endGroup();

    m_settings->sync();
}

void AppConfig::load(const QString& configPath)
{
    m_path = configPath;

    // 确保目录存在
    QDir().mkpath(QFileInfo(configPath).absolutePath());

    m_settings = std::make_unique<QSettings>(configPath, QSettings::IniFormat);

    // 写入缺失的默认值（首次运行时生成完整配置文件）
    applyDefaults();

    // ── 读取 [log] 节 ────────────────────────────────────
    m_settings->beginGroup(QStringLiteral("log"));

    m_logLevel = parseLevel(
        m_settings->value(QStringLiteral("level"), QStringLiteral("debug")).toString(),
        spdlog::level::debug);

    m_logDir = m_settings->value(QStringLiteral("dir"), QStringLiteral("logs")).toString();

    m_logMaxFileMB = m_settings->value(QStringLiteral("max_file_size_mb"), 5).toInt();
    if (m_logMaxFileMB <= 0) m_logMaxFileMB = 5;

    m_logMaxFiles = m_settings->value(QStringLiteral("max_files"), 3).toInt();
    if (m_logMaxFiles <= 0) m_logMaxFiles = 3;

    m_logFlushOn = parseLevel(
        m_settings->value(QStringLiteral("flush_on"), QStringLiteral("warn")).toString(),
        spdlog::level::warn);

    m_settings->endGroup();

    // ── 读取 [ui] 节 ─────────────────────────────────────
    m_settings->beginGroup(QStringLiteral("ui"));
    setThemeName(m_settings->value(QStringLiteral("theme"), QStringLiteral("dark")).toString());
    m_settings->endGroup();
}

void AppConfig::setThemeName(const QString& themeName)
{
    const QString normalized = themeName.trimmed().toLower();
    static const QRegularExpression validThemeId(QStringLiteral("^[a-z0-9_-]+$"));
    m_themeName = validThemeId.match(normalized).hasMatch()
        ? normalized
        : QStringLiteral("dark");
}

void AppConfig::save()
{
    if (!m_settings) return;

    m_settings->beginGroup(QStringLiteral("log"));
    m_settings->setValue(QStringLiteral("level"),            levelToString(m_logLevel));
    m_settings->setValue(QStringLiteral("dir"),              m_logDir);
    m_settings->setValue(QStringLiteral("max_file_size_mb"), m_logMaxFileMB);
    m_settings->setValue(QStringLiteral("max_files"),        m_logMaxFiles);
    m_settings->setValue(QStringLiteral("flush_on"),         levelToString(m_logFlushOn));
    m_settings->endGroup();

    m_settings->beginGroup(QStringLiteral("ui"));
    m_settings->setValue(QStringLiteral("theme"), m_themeName);
    m_settings->endGroup();

    m_settings->sync();
}
