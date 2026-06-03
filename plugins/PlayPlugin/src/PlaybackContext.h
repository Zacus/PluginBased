#pragma once

#include <QObject>
#include <QQmlEngine>
#include <functional>
#include "IAppPlugin.h"
#include "IPlayerPlugin.h"

// 前向声明，避免循环依赖
class PlayerEngine;

/**
 * @brief PlaybackContext —— PlayPlugin 模块内部的播放上下文单例
 *
 * 职责：
 *   1. 持有播放器能力查找器，供 PlayerEngine::open() 查找解码插件
 *   2. 持有 PlayerEngine* 弱引用，供 PlayPlugin（IPlayerPlugin 实现）
 *      查询 duration/position/isPlaying 状态，无需直接依赖 PlayerEngine.h
 */
class PlaybackContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using PlayerPluginFinder = PluginContext::PlayerPluginFinder;

    static PlaybackContext* create(QQmlEngine*, QJSEngine*)
    {
        auto& ctx = instance();
        QQmlEngine::setObjectOwnership(&ctx, QQmlEngine::CppOwnership);
        return &ctx;
    }

    static PlaybackContext& instance()
    {
        static PlaybackContext s;
        return s;
    }

    // ── 播放器能力查找器 ─────────────────────────────────────────────────
    void setFinder(PlayerPluginFinder finder) { m_finder = std::move(finder); }
    void clearFinder()                  { m_finder = {}; }
    bool hasFinder() const              { return static_cast<bool>(m_finder); }

    IPlayerPlugin* findPlugin(const QUrl& url) const
    {
        return m_finder ? m_finder(url) : nullptr;
    }

    // ── 宿主 open 时序容错 ───────────────────────────────────────────────
    void setPendingOpenUrl(const QUrl& url) { m_pendingOpenUrl = url; }
    QUrl takePendingOpenUrl()
    {
        const QUrl url = m_pendingOpenUrl;
        m_pendingOpenUrl = QUrl();
        return url;
    }
    void clearPendingOpenUrl() { m_pendingOpenUrl = QUrl(); }

    // ── PlayerEngine 注册（由 PlayerEngine 构造/析构时调用）──────────────
    // 使用裸指针+手动注册，避免头文件循环依赖
    // PlayPlugin 通过此接口查询状态，无需 #include "PlayerEngine.h"
    void registerEngine(PlayerEngine* engine)   { m_engine = engine; }
    void unregisterEngine(PlayerEngine* engine) { if (m_engine == engine) m_engine = nullptr; }
    PlayerEngine* engine() const                { return m_engine; }

private:
    explicit PlaybackContext(QObject* parent = nullptr) : QObject(parent) {}

    PlayerPluginFinder m_finder;
    PlayerEngine* m_engine = nullptr;
    QUrl          m_pendingOpenUrl;
};
