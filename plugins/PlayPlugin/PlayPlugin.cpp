#include "PlayPlugin.h"
#include "Logger.h"

PlayPlugin::PlayPlugin(QObject* parent)
    : QObject(parent)
{}

PlayPlugin::~PlayPlugin()
{
    shutdown();
}

bool PlayPlugin::initialize()
{
    LOG_INFO("PlayPlugin::initialize()");
    return true;
}

void PlayPlugin::shutdown()
{
    LOG_INFO("PlayPlugin::shutdown()");
    m_playing = false;
}

bool PlayPlugin::canHandle(const QUrl& url) const
{
    // 支持所有本地视频 / 音频文件
    if (!url.isLocalFile())
        return false;

    static const QStringList exts = {
        "mp4", "mkv", "avi", "mov", "flv", "webm",
        "mp3", "flac", "aac", "ogg", "wav"
    };
    const QString suffix = url.fileName().section('.', -1).toLower();
    return exts.contains(suffix);
}

bool PlayPlugin::open(const QUrl& url)
{
    m_currentUrl = url;
    m_position   = 0;
    m_playing    = false;
    LOG_INFO("PlayPlugin::open({})", url.toString().toStdString());
    return true;
}

void PlayPlugin::play()
{
    m_playing = true;
    LOG_INFO("PlayPlugin::play()");
}

void PlayPlugin::pause()
{
    m_playing = false;
    LOG_INFO("PlayPlugin::pause()");
}

void PlayPlugin::stop()
{
    m_playing  = false;
    m_position = 0;
    LOG_INFO("PlayPlugin::stop()");
}

void PlayPlugin::seek(qint64 positionMs)
{
    m_position = qBound(0LL, positionMs, m_duration);
    LOG_DEBUG("PlayPlugin::seek({}ms)", positionMs);
}

QUrl PlayPlugin::qmlComponentUrl() const
{
    // qrc 路径由 CMakeLists qt_add_resources 注册，前缀为 /PlayPlugin
    return QUrl(QStringLiteral("qrc:/PlayPlugin/qml/PlayPluginView.qml"));
}
