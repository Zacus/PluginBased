#pragma once

#include <QObject>
#include <QQmlEngine>
#include <functional>
#include "IPlayerPlugin.h"

/**
 * @brief PlaybackContext —— PlayPlugin 模块内部的播放上下文单例
 *
 * ## 职责
 * 持有 PluginFinder，作为 PlayPlugin 模块内所有需要查找解码插件的
 * C++ 类（目前是 PlayerEngine）的统一取值点。
 *
 * ## 为什么替换静态注册表
 * 原来的 `static PlayerEngine::PluginFinder s_globalFinder` 存在三个问题：
 *   1. 裸全局变量，生命周期和所有权语义不明确
 *   2. `registerPluginFinder` 是 PlayerEngine 的静态方法，
 *      让"播放引擎"类承担了"依赖注入容器"的职责，违反单一职责
 *   3. 每个 PlayerEngine 实例在构造时拷贝一次 finder，
 *      shutdown() 写 nullptr 后仍有已构造实例持有旧拷贝
 *
 * ## 改进点
 *   - 生命周期由 PlayPlugin 控制：initialize() → setFinder()，shutdown() → clearFinder()
 *   - PlayerEngine::open() 每次调用时实时从此处取 finder，无拷贝无缓存
 *   - 注册为 PlayPlugin 1.0 的 QML_SINGLETON，未来 QML 层也可直接访问播放状态
 *
 * ## 线程安全
 * setFinder / finder 均在主线程调用（Qt 的 QML 引擎和 PlayerEngine 均在主线程），
 * 无需加锁。若未来需要跨线程，在此处加 QMutex 即可，调用方无需改动。
 */
class PlaybackContext : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using PluginFinder = IPlayerPlugin::PluginFinder;

    // QML_SINGLETON 要求的静态工厂函数
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

    /** 设置插件查找器（由 PlayPlugin::initialize() 调用） */
    void setFinder(PluginFinder finder) { m_finder = std::move(finder); }

    /** 清除查找器（由 PlayPlugin::shutdown() 调用） */
    void clearFinder() { m_finder = {}; }

    /**
     * @brief 查找能处理指定 URL 的解码插件
     *
     * PlayerEngine::open() 每次调用此函数，实时取最新 finder，
     * 避免构造时拷贝导致 shutdown 后仍持有旧引用的问题。
     */
    IPlayerPlugin* findPlugin(const QUrl& url) const
    {
        return m_finder ? m_finder(url) : nullptr;
    }

    bool hasFinder() const { return static_cast<bool>(m_finder); }

private:
    explicit PlaybackContext(QObject* parent = nullptr) : QObject(parent) {}

    PluginFinder m_finder;
};
