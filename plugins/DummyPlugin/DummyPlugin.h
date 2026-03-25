#pragma once

#include <QObject>
#include "IPlayerPlugin.h"

/**
 * @brief 示例插件 —— 不做真实解码，仅验证插件框架运转
 *
 * 生产插件（如 FFmpeg 插件）照此接口实现即可。
 */
class DummyPlugin : public QObject, public IPlayerPlugin
{
    Q_OBJECT
    Q_INTERFACES(IPlayerPlugin)
    Q_PLUGIN_METADATA(IID IPlayerPlugin_IID FILE "DummyPlugin.json")

public:
    explicit DummyPlugin(QObject* parent = nullptr);
    ~DummyPlugin() override;

    // ── 元信息 ────────────────────────────────────────────────────────────
    QString name()        const override { return QStringLiteral("DummyPlugin"); }
    QString version()     const override { return QStringLiteral("1.0.0"); }
    QString description() const override { return QStringLiteral("Stub plugin for framework validation"); }

    // ── 生命周期 ──────────────────────────────────────────────────────────
    bool initialize(PluginFinder /*finder*/ = {}) override;
    void shutdown()   override;

    // ── 能力查询 ──────────────────────────────────────────────────────────
    bool canHandle(const QUrl& url) const override;

    // ── 播放控制 ──────────────────────────────────────────────────────────
    bool open(const QUrl& url) override;
    void play()                override;
    void pause()               override;
    void stop()                override;
    void seek(qint64 positionMs) override;

    // ── 状态查询 ──────────────────────────────────────────────────────────
    qint64 duration()  const override { return m_duration; }
    qint64 position()  const override { return m_position; }
    bool   isPlaying() const override { return m_playing; }

private:
    QUrl   m_url;
    qint64 m_duration = 60'000;   // 假设 60 秒
    qint64 m_position = 0;
    bool   m_playing  = false;
};
