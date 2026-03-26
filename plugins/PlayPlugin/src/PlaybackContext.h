#pragma once

#include <QObject>
#include <QQmlEngine>
#include <functional>
#include "IPlayerPlugin.h"

/**
 * @brief PlaybackContext —— PlayPlugin 模块内部的播放上下文单例
 *
 * ## 职责
 *   持有 PluginFinder，供 PlayerEngine::open() 查找解码插件。
 *
 * ## 日志
 *   插件 QML 的日志改由 AppLog 1.0 模块的 Log 单例承担，
 *   PlaybackContext 不再重复实现 logXxx 方法。
 *   任何需要日志的插件 QML 只需：
 *     import AppLog 1.0
 *     Log.info("...")
 *
 * ## 线程安全
 *   所有方法均在主线程调用（QML 引擎单线程），无需加锁。
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

    void setFinder(PluginFinder finder) { m_finder = std::move(finder); }
    void clearFinder()                  { m_finder = {}; }
    bool hasFinder() const              { return static_cast<bool>(m_finder); }

    IPlayerPlugin* findPlugin(const QUrl& url) const
    {
        return m_finder ? m_finder(url) : nullptr;
    }

private:
    explicit PlaybackContext(QObject* parent = nullptr) : QObject(parent) {}

    PluginFinder m_finder;
};
