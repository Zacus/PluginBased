#include "PlayPlugin.h"
#include "PlaybackContext.h"
#include "PlayerEngine.h"
#include "Logger.h"

// FFmpeg — C 库，必须用 extern "C" 包裹
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// 编译期版本断言：要求 FFmpeg >= 5.0（libavutil major >= 57）
static_assert(LIBAVUTIL_VERSION_MAJOR >= 57,
    "FFmpeg >= 5.0 is required (libavutil major >= 57)");

// ── 辅助：安全获取 PlayerEngine（可能为 nullptr，由 QML 侧管理生命周期）────
static PlayerEngine* engine()
{
    return PlaybackContext::instance().engine();
}

// ─────────────────────────────────────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────────────────────────────────────
PlayPlugin::PlayPlugin(QObject* parent)
    : QObject(parent)
{}

PlayPlugin::~PlayPlugin()
{
    shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// 生命周期
// ─────────────────────────────────────────────────────────────────────────────
bool PlayPlugin::initialize(PluginFinder finder)
{
    LOG_INFO("PlayPlugin::initialize()");
    PlaybackContext::instance().setFinder(std::move(finder));
    return true;
}

void PlayPlugin::shutdown()
{
    LOG_INFO("PlayPlugin::shutdown()");
    if (auto* e = engine())
        e->stop();
    PlaybackContext::instance().clearPendingOpenUrl();
    PlaybackContext::instance().clearFinder();
}

// ─────────────────────────────────────────────────────────────────────────────
// 能力查询
// ─────────────────────────────────────────────────────────────────────────────
bool PlayPlugin::canHandle(const QUrl& url) const
{
    if (!url.isLocalFile()) return false;
    static const QStringList exts = {
        "mp4", "mkv", "avi", "mov", "flv", "webm",
        "mp3", "flac", "aac", "ogg", "wav", "m4a", "m4v", "ts", "rmvb"
    };
    return exts.contains(url.fileName().section('.', -1).toLower());
}

// ─────────────────────────────────────────────────────────────────────────────
// 播放控制（转发给 PlayerEngine）
// ─────────────────────────────────────────────────────────────────────────────
bool PlayPlugin::open(const QUrl& url)
{
    LOG_INFO("PlayPlugin::open({})", url.toString().toStdString());
    if (auto* e = engine()) {
        e->open(url);
        return true;
    }
    PlaybackContext::instance().setPendingOpenUrl(url);
    LOG_WARN("PlayPlugin::open() — PlayerEngine not available yet, deferred until QML loads");
    return true;
}

void PlayPlugin::play()
{
    if (auto* e = engine()) e->play();
}

void PlayPlugin::pause()
{
    if (auto* e = engine()) e->pause();
}

void PlayPlugin::stop()
{
    if (auto* e = engine()) e->stop();
}

void PlayPlugin::seek(qint64 positionMs)
{
    if (auto* e = engine()) e->seek(positionMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// 状态查询（从 PlayerEngine 实时读取）
// ─────────────────────────────────────────────────────────────────────────────
qint64 PlayPlugin::duration() const
{
    if (auto* e = engine()) return e->duration();
    return 0;
}

qint64 PlayPlugin::position() const
{
    if (auto* e = engine()) return e->position();
    return 0;
}

bool PlayPlugin::isPlaying() const
{
    if (auto* e = engine())
        return e->playbackState() == PlayerEngine::Playing;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// QML UI
// ─────────────────────────────────────────────────────────────────────────────
QUrl PlayPlugin::qmlComponentUrl() const
{
    return QUrl(QStringLiteral("qrc:/PlayPlugin/qml/PlayPluginView.qml"));
}
