#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QTimer>
#include <QQmlEngine>
#include <functional>

#include "MediaInfo.h"
#include "IPlayerPlugin.h"

/**
 * @brief 播放引擎 —— 归属 PlayPlugin 模块，与 PluginManager 完全解耦
 *
 * ## 依赖注入机制
 *
 * QML 通过 `PlayerEngine {}` 实例化本类，无法在构造时传参。
 * 采用「静态注册表」模式解决：
 *
 *   1. PlayPlugin::initialize() 调用 PlayerEngine::registerPluginFinder()
 *      将 PluginManager::findPlugin 包装后写入静态注册表。
 *   2. 每个 PlayerEngine 实例在构造时从注册表取出 finder 存入成员变量。
 *   3. open() 时通过 finder 查找能处理该 URL 的 IPlayerPlugin。
 *
 * 如此 PlayerEngine 的头文件无需 include PluginManager.h，
 * 插件与宿主之间保持单向依赖（宿主 → 插件），无循环依赖。
 *
 * ## QML 注册
 * 类型注册在 URI="PlayPlugin" Version=1.0 模块，
 * PlayPlugin 的 qt_add_qml_module 负责生成对应的 qmldir 和类型注册代码。
 */
class PlayerEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int        playbackState READ playbackStateInt NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64     position      READ position         NOTIFY positionChanged)
    Q_PROPERTY(qint64     duration      READ duration         NOTIFY durationChanged)
    Q_PROPERTY(float      volume        READ volume  WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool       muted         READ muted   WRITE setMuted  NOTIFY mutedChanged)
    Q_PROPERTY(MediaInfo* currentMedia  READ currentMedia     NOTIFY currentMediaChanged)
    Q_PROPERTY(QString    errorString   READ errorString      NOTIFY errorOccurred)

public:
    using PluginFinder = std::function<IPlayerPlugin*(const QUrl&)>;

    /**
     * @brief 注册全局 PluginFinder（由 PlayPlugin::initialize() 调用）
     *
     * 线程安全性：必须在 QML 引擎创建任何 PlayerEngine 实例之前调用，
     * 即在 PlayPlugin::initialize() 阶段完成注册。
     */
    static void registerPluginFinder(PluginFinder finder);

    enum PlaybackState { Stopped = 0, Playing = 1, Paused = 2 };
    Q_ENUM(PlaybackState)

    explicit PlayerEngine(QObject* parent = nullptr);
    ~PlayerEngine() override;

    PlaybackState playbackState()    const { return m_state; }
    int           playbackStateInt() const { return static_cast<int>(m_state); }
    qint64        position()         const;
    qint64        duration()         const;
    float         volume()           const { return m_volume; }
    bool          muted()            const { return m_muted; }
    MediaInfo*    currentMedia()     const { return m_mediaInfo; }
    QString       errorString()      const { return m_errorString; }

    void setVolume(float v);
    void setMuted(bool m);

public slots:
    Q_INVOKABLE void open(const QUrl& url);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void togglePlayPause();

signals:
    void playbackStateChanged(int state);
    void positionChanged(qint64 posMs);
    void durationChanged(qint64 durMs);
    void volumeChanged(float volume);
    void mutedChanged(bool muted);
    void currentMediaChanged(MediaInfo* info);
    void errorOccurred(const QString& msg);
    void endOfMedia();

private:
    void setState(PlaybackState s);
    void setError(const QString& msg);

    PluginFinder   m_pluginFinder;
    IPlayerPlugin* m_plugin    = nullptr;
    MediaInfo*     m_mediaInfo = nullptr;
    PlaybackState  m_state     = Stopped;
    float          m_volume    = 1.0f;
    bool           m_muted     = false;
    QString        m_errorString;
    QTimer         m_positionTimer;
};
