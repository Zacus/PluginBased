#include "PlayPlugin.h"
#include "PlayerEngine.h"    // PlayPlugin/src/PlayerEngine.h（via include_directories）
#include "PluginManager.h"   // core/PluginManager.h（宿主层，仅在 initialize() 中引用）
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

    // ── 依赖注入：向 PlayerEngine 静态注册表写入 PluginFinder ─────────────
    //
    // PlayerEngine 由 QML 实例化（PlayerView.qml 中的 PlayerEngine {}），
    // 每个实例在构造时从静态注册表取 finder，在 open() 时调用它查找解码插件。
    //
    // 此处是 PlayPlugin 与宿主 PluginManager 唯一的耦合点，
    // 且方向是：宿主 → 插件（PlayPlugin::initialize 被宿主调用），
    // 插件不持有 PluginManager 的指针，仅在这里借用其方法包一个 lambda 注入。
    PlayerEngine::registerPluginFinder([](const QUrl& url) -> IPlayerPlugin* {
        return PluginManager::instance().findPlugin(url);
    });

    LOG_INFO("PlayPlugin: PluginFinder registered into PlayerEngine");
    return true;
}

void PlayPlugin::shutdown()
{
    LOG_INFO("PlayPlugin::shutdown()");
    m_playing = false;
    PlayerEngine::registerPluginFinder(nullptr);   // 清除，避免 shutdown 后悬空引用
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
    // qrc 路径：PlayPlugin.so 通过 qt6_add_resources 内嵌，前缀 /PlayPlugin/qml/
    return QUrl(QStringLiteral("qrc:/PlayPlugin/qml/PlayPluginView.qml"));
}
