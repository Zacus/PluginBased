#pragma once

#include <QObject>
#include <QUrl>
#include "IPlayerPlugin.h"

/**
 * @brief PlayPlugin —— 播放器界面插件（完全自包含）
 *
 * 本插件封装了播放器全部业务逻辑与 UI：
 *   - C++ 业务层：PlayerEngine / PlaylistModel / MediaInfo
 *   - QML 界面层：PlayPluginView / PlayerView / ControlBar / PlaylistView
 *
 * 与宿主的唯一耦合点：
 *   1. IPlayerPlugin 接口（播放控制协议）
 *   2. PluginManager::findPlugin（通过依赖注入，在 initialize() 中绑定）
 *   3. AppController::log*（QML 层通过 VideoPlayer 1.0 模块访问，属于 app 基础设施）
 *
 * QML 类型注册在独立模块 URI="PlayPlugin" Version=1.0，
 * 与宿主的 "VideoPlayer" 模块互不污染。
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
    bool initialize(PluginFinder finder = {}) override;
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
    bool hasQmlUI()        const override { return true; }
    QUrl qmlComponentUrl() const override;

private:
    QUrl   m_currentUrl;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    bool   m_playing  = false;
};
