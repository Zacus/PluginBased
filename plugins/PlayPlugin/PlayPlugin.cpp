#include "PlayPlugin.h"
#include "PlaybackContext.h"
#include "Logger.h"

// FFmpeg — C 库，必须用 extern "C" 包裹
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// 编译期版本断言：要求 FFmpeg >= 5.0（libavutil >= 57.0.0）
static_assert(LIBAVUTIL_VERSION_MAJOR >= 57,
    "FFmpeg >= 5.0 is required (libavutil major >= 57)");

PlayPlugin::PlayPlugin(QObject* parent)
    : QObject(parent)
{}

PlayPlugin::~PlayPlugin()
{
    shutdown();
}

bool PlayPlugin::initialize(PluginFinder finder)
{
    LOG_INFO("PlayPlugin::initialize()");

    // 将宿主注入的 finder 存入 PlaybackContext。
    // PlayerEngine::open() 每次调用时实时从 PlaybackContext 取，
    // 不存在"构造后 finder 已过期"的问题。
    PlaybackContext::instance().setFinder(std::move(finder));

    LOG_INFO("PlayPlugin: PluginFinder stored in PlaybackContext");
    return true;
}

void PlayPlugin::shutdown()
{
    LOG_INFO("PlayPlugin::shutdown()");
    m_playing = false;

    // 清除 finder：所有现存 PlayerEngine 实例下次调用 open() 时
    // 会从 PlaybackContext 取到空 finder 并报错，而非持有悬空引用。
    PlaybackContext::instance().clearFinder();
}

bool PlayPlugin::canHandle(const QUrl& url) const
{
    if (!url.isLocalFile()) return false;
    static const QStringList exts = {
        "mp4", "mkv", "avi", "mov", "flv", "webm",
        "mp3", "flac", "aac", "ogg", "wav"
    };
    return exts.contains(url.fileName().section('.', -1).toLower());
}

bool PlayPlugin::open(const QUrl& url)
{
    m_currentUrl = url;
    m_position   = 0;
    m_playing    = false;
    LOG_INFO("PlayPlugin::open({})", url.toString().toStdString());
    return true;
}

void PlayPlugin::play()  { m_playing = true;  LOG_INFO("PlayPlugin::play()");  }
void PlayPlugin::pause() { m_playing = false; LOG_INFO("PlayPlugin::pause()"); }
void PlayPlugin::stop()  { m_playing = false; m_position = 0; LOG_INFO("PlayPlugin::stop()"); }

void PlayPlugin::seek(qint64 positionMs)
{
    m_position = qBound(0LL, positionMs, m_duration);
    LOG_DEBUG("PlayPlugin::seek({}ms)", positionMs);
}

QUrl PlayPlugin::qmlComponentUrl() const
{
    return QUrl(QStringLiteral("qrc:/PlayPlugin/qml/PlayPluginView.qml"));
}
