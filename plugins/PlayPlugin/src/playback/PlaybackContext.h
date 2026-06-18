#pragma once

#include <QObject>
#include <QQmlEngine>
#include "IAppPlugin.h"

// 前向声明，避免循环依赖
class PlayerEngine;

/**
 * @brief PlaybackContext —— PlayPlugin 模块内部的播放上下文单例
 *
 * 职责：
 *   1. 持有 PlayerEngine* 弱引用，供 PlayPlugin 生命周期处理使用
 */
class PlaybackContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
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

    // ── PlayerEngine 注册（由 PlayerEngine 构造/析构时调用）──────────────
    // 使用裸指针+手动注册，避免头文件循环依赖
    void registerEngine(PlayerEngine* engine)   { m_engine = engine; }
    void unregisterEngine(PlayerEngine* engine) { if (m_engine == engine) m_engine = nullptr; }
    PlayerEngine* engine() const                { return m_engine; }

private:
    explicit PlaybackContext(QObject* parent = nullptr) : QObject(parent) {}

    PlayerEngine* m_engine = nullptr;
};
