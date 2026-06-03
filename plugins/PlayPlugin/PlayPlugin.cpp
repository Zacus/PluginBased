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
bool PlayPlugin::initialize(const PluginContext&)
{
    LOG_INFO("PlayPlugin::initialize()");
    return true;
}

void PlayPlugin::shutdown()
{
    LOG_INFO("PlayPlugin::shutdown()");
    if (auto* e = engine())
        e->stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// QML UI
// ─────────────────────────────────────────────────────────────────────────────
QUrl PlayPlugin::qmlComponentUrl() const
{
    return QUrl(QStringLiteral("qrc:/PlayPlugin/qml/PlayPluginView.qml"));
}
