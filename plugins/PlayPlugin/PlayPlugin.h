#pragma once

#include <QObject>
#include <QUrl>
#include "IAppPlugin.h"

/**
 * @brief PlayPlugin —— 播放器界面插件（完全自包含）
 *
 * 真正的播放逻辑由插件内部的 PlayerEngine（QML 侧实例化）承担。
 *
 * 生命周期：
 *   宿主 PluginManager::loadAll() → 调用 initialize()
 *   宿主卸载 → 调用 shutdown()
 *   PlayerEngine 由 QML 引擎管理，生命周期独立于 PlayPlugin
 */
class PlayPlugin : public QObject, public IAppPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "PlayPlugin.json")

public:
    explicit PlayPlugin(QObject* parent = nullptr);
    ~PlayPlugin() override;

    // ── 元信息 ────────────────────────────────────────────────────────────
    QString id()          const override { return QStringLiteral("play"); }
    QString name()        const override { return QStringLiteral("PlayPlugin"); }
    QString version()     const override { return QStringLiteral("1.0.0"); }
    QString description() const override { return QStringLiteral("内置播放器：视频 + 播放列表"); }
    QString cardIcon()    const override { return QStringLiteral("▶"); }
    QString cardName()    const override { return QStringLiteral("视频播放器"); }

    // ── 生命周期 ──────────────────────────────────────────────────────────
    bool initialize(const PluginContext& context) override;
    void shutdown()                          override;

    // ── QML UI ────────────────────────────────────────────────────────────
    bool hasQmlUI()        const override { return true; }
    QUrl qmlComponentUrl() const override;
};
