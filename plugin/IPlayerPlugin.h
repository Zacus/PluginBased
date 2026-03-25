#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>
#include <functional>

/**
 * @brief 播放器插件纯虚接口
 *
 * 每个插件动态库都必须：
 *  1. 继承此接口并实现所有纯虚方法
 *  2. 在 .cpp 中添加 Q_PLUGIN_METADATA(IID IPlayerPlugin_IID)
 *  3. CMakeLists 链接为 MODULE 库
 */
#define IPlayerPlugin_IID "com.videoplayer.IPlayerPlugin/1.0"

class IPlayerPlugin
{
public:
    virtual ~IPlayerPlugin() = default;

    /**
     * @brief 插件查找器类型
     *
     * 由宿主 PluginManager 在调用 initialize() 时构造并传入。
     * 插件通过此函数查找能处理指定 URL 的解码插件，
     * 无需 #include PluginManager.h，彻底断开对宿主层的静态依赖。
     */
    using PluginFinder = std::function<IPlayerPlugin*(const QUrl&)>;

    // ── 元信息 ────────────────────────────────────────────────────────────
    /** 插件唯一名称，例如 "FFmpegPlugin" */
    virtual QString name()        const = 0;
    /** 版本字符串，例如 "1.0.0" */
    virtual QString version()     const = 0;
    /** 简短描述 */
    virtual QString description() const = 0;

    // ── 生命周期 ──────────────────────────────────────────────────────────
    /**
     * @brief 插件加载后由 PluginManager 调用，返回 false 表示初始化失败
     *
     * @param finder 宿主注入的插件查找器，插件可用它查找其他已加载插件。
     *               不需要此能力的插件（如 DummyPlugin）可忽略该参数。
     */
    virtual bool initialize(PluginFinder finder = {}) = 0;
    /** 插件卸载前调用 */
    virtual void shutdown() = 0;

    // ── 能力查询 ──────────────────────────────────────────────────────────
    /** 插件是否支持指定的媒体 URL/扩展名 */
    virtual bool canHandle(const QUrl& url) const = 0;

    // ── 播放控制 ──────────────────────────────────────────────────────────
    virtual bool open(const QUrl& url)  = 0;
    virtual void play()                 = 0;
    virtual void pause()                = 0;
    virtual void stop()                 = 0;
    virtual void seek(qint64 positionMs) = 0;

    // ── 状态查询 ──────────────────────────────────────────────────────────
    virtual qint64 duration()   const = 0;  ///< 总时长 (ms)
    virtual qint64 position()   const = 0;  ///< 当前位置 (ms)
    virtual bool   isPlaying()  const = 0;

    // ── UI 能力（可选覆盖）────────────────────────────────────────────────
    /**
     * @brief 插件是否提供自己的 QML 播放界面
     *
     * 返回 true 时，宿主会调用 qmlComponentUrl() 加载插件 QML 页面；
     * 返回 false（默认）时，宿主使用内置播放器视图。
     */
    virtual bool hasQmlUI() const { return false; }

    /**
     * @brief 插件 QML 入口文件的 URL
     *
     * 仅在 hasQmlUI() == true 时有效。
     * 返回的 URL 必须是插件可访问的 qrc:/ 路径或绝对文件路径。
     *
     * 示例：return QUrl("qrc:/PlayPlugin/PlayPluginView.qml");
     */
    virtual QUrl qmlComponentUrl() const { return QUrl{}; }

    /**
     * @brief 插件在 HomePanel 显示的图标（emoji 或单字符）
     */
    virtual QString cardIcon() const { return QStringLiteral("▶"); }

    /**
     * @brief 插件在 HomePanel 显示的名称（可与 name() 不同）
     */
    virtual QString cardName() const { return name(); }
};

Q_DECLARE_INTERFACE(IPlayerPlugin, IPlayerPlugin_IID)
