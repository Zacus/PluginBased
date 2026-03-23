#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>

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

    // ── 元信息 ────────────────────────────────────────────────────────────
    /** 插件唯一名称，例如 "FFmpegPlugin" */
    virtual QString name()        const = 0;
    /** 版本字符串，例如 "1.0.0" */
    virtual QString version()     const = 0;
    /** 简短描述 */
    virtual QString description() const = 0;

    // ── 生命周期 ──────────────────────────────────────────────────────────
    /** 插件加载后由 PluginManager 调用，返回 false 表示初始化失败 */
    virtual bool initialize() = 0;
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
};

Q_DECLARE_INTERFACE(IPlayerPlugin, IPlayerPlugin_IID)
