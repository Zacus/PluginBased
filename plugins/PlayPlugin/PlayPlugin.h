#pragma once

#include <QObject>
#include <QUrl>
#include "IPlayerPlugin.h"

/**
 * @brief PlayPlugin —— 内置播放器界面插件
 *
 * 将原来硬编码在 app 层的播放页面（PlayerView / ControlBar / PlaylistView）
 * 封装为一个标准 IPlayerPlugin，宿主通过 qmlComponentUrl() 加载其 QML 界面。
 *
 * 媒体解码仍由 PlayerEngine（core 层）负责；
 * 本插件只负责 UI 呈现 + 简单的播放控制转发。
 */
class PlayPlugin : public QObject, public IPlayerPlugin
{
    Q_OBJECT
    Q_INTERFACES(IPlayerPlugin)
    Q_PLUGIN_METADATA(IID IPlayerPlugin_IID FILE "PlayPlugin.json")

public:
    explicit PlayPlugin(QObject* parent = nullptr);
    ~PlayPlugin() override;

    // ── 元信息 ────────────────────────────────────────────────────────────
    QString name()        const override { return QStringLiteral("PlayPlugin"); }
    QString version()     const override { return QStringLiteral("1.0.0"); }
    QString description() const override { return QStringLiteral("内置播放器界面：视频播放 + 播放列表"); }

    // ── HomePanel 显示 ────────────────────────────────────────────────────
    QString cardIcon() const override { return QStringLiteral("▶"); }
    QString cardName() const override { return QStringLiteral("视频播放器"); }

    // ── 生命周期 ──────────────────────────────────────────────────────────
    bool initialize() override;
    void shutdown()   override;

    // ── 能力查询 ──────────────────────────────────────────────────────────
    bool canHandle(const QUrl& url) const override;

    // ── 播放控制（转发给 PlayerEngine；UI 侧由 QML 直接调用 engine） ──────
    bool open(const QUrl& url) override;
    void play()                override;
    void pause()               override;
    void stop()                override;
    void seek(qint64 positionMs) override;

    // ── 状态查询 ──────────────────────────────────────────────────────────
    qint64 duration()  const override { return m_duration; }
    qint64 position()  const override { return m_position; }
    bool   isPlaying() const override { return m_playing;  }

    // ── QML UI 能力 ───────────────────────────────────────────────────────
    bool hasQmlUI()         const override { return true; }
    QUrl qmlComponentUrl()  const override;

private:
    QUrl   m_currentUrl;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    bool   m_playing  = false;
};
