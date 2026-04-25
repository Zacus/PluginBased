#pragma once

#include <QObject>
#include <QQmlEngine>
#include <functional>
#include "IPlayerPlugin.h"

// 前向声明，避免循环依赖
class PlayerEngine;

/**
 * @brief PlaybackContext —— PlayPlugin 模块内部的播放上下文单例
 *
 * 职责：
 *   1. 持有 PluginFinder，供 PlayerEngine::open() 查找解码插件
 *   2. 持有 PlayerEngine* 弱引用，供 PlayPlugin（IPlayerPlugin 实现）
 *      查询 duration/position/isPlaying 状态，无需直接依赖 PlayerEngine.h
 */
class PlaybackContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using PluginFinder = IPlayerPlugin::PluginFinder;

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

    // ── PluginFinder ──────────────────────────────────────────────────────
    void setFinder(PluginFinder finder) { m_finder = std::move(finder); }
    void clearFinder()                  { m_finder = {}; }
    bool hasFinder() const              { return static_cast<bool>(m_finder); }

    IPlayerPlugin* findPlugin(const QUrl& url) const
    {
        return m_finder ? m_finder(url) : nullptr;
    }

    // ── PlayerEngine 注册（由 PlayerEngine 构造/析构时调用）──────────────
    // 使用裸指针+手动注册，避免头文件循环依赖
    // PlayPlugin 通过此接口查询状态，无需 #include "PlayerEngine.h"
    void registerEngine(PlayerEngine* engine)   { m_engine = engine; }
    void unregisterEngine(PlayerEngine* engine) { if (m_engine == engine) m_engine = nullptr; }
    PlayerEngine* engine() const                { return m_engine; }

private:
    explicit PlaybackContext(QObject* parent = nullptr) : QObject(parent) {}

    PluginFinder  m_finder;
    PlayerEngine* m_engine = nullptr;
};
