#pragma once

#include <QObject>
#include <QUrl>
#include "IPlayerPlugin.h"

/**
 * @brief PlayPlugin —— 播放器界面插件（完全自包含）
 *
 * IPlayerPlugin 实现层：负责与宿主对接（canHandle / open / play 等接口）。
 * 真正的播放逻辑由 PlayerEngine（QML 侧实例化）承担，
 * PlayPlugin 通过 PlaybackContext::engine() 查询状态、转发控制指令。
 *
 * 生命周期：
 *   宿主 PluginManager::loadAll() → 调用 initialize()
 *   宿主卸载 → 调用 shutdown()
 *   PlayerEngine 由 QML 引擎管理，生命周期独立于 PlayPlugin
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
    QString description() const override { return QStringLiteral("内置播放器：视频 + 播放列表"); }
    QString cardIcon()    const override { return QStringLiteral("▶"); }
    QString cardName()    const override { return QStringLiteral("视频播放器"); }

    // ── 生命周期 ──────────────────────────────────────────────────────────
    bool initialize(PluginFinder finder = {}) override;
    void shutdown()                          override;

    // ── 能力查询 ──────────────────────────────────────────────────────────
    bool canHandle(const QUrl& url) const override;

    // ── 播放控制（转发给 PlayerEngine）───────────────────────────────────
    bool open(const QUrl& url)      override;
    void play()                     override;
    void pause()                    override;
    void stop()                     override;
    void seek(qint64 positionMs)    override;

    // ── 状态查询（从 PlayerEngine 实时读取）─────────────────────────────
    qint64 duration()  const override;
    qint64 position()  const override;
    bool   isPlaying() const override;

    // ── QML UI ────────────────────────────────────────────────────────────
    bool hasQmlUI()        const override { return true; }
    QUrl qmlComponentUrl() const override;
};
