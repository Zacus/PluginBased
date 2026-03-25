#include "PlayerEngine.h"
#include "PlaybackContext.h"
#include "Logger.h"

#include <QFileInfo>

PlayerEngine::PlayerEngine(QObject* parent)
    : QObject(parent)
{
    m_positionTimer.setInterval(250);
    connect(&m_positionTimer, &QTimer::timeout, this, [this] {
        if (m_plugin && m_state == Playing)
            emit positionChanged(m_plugin->position());
    });
    LOG_DEBUG("PlayerEngine created");
}

PlayerEngine::~PlayerEngine()
{
    stop();
    LOG_DEBUG("PlayerEngine destroyed");
}

qint64 PlayerEngine::position() const { return m_plugin ? m_plugin->position() : 0LL; }
qint64 PlayerEngine::duration() const { return m_plugin ? m_plugin->duration() : 0LL; }

void PlayerEngine::setVolume(float v)
{
    v = qBound(0.0f, v, 1.0f);
    if (qFuzzyCompare(m_volume, v)) return;
    m_volume = v;
    emit volumeChanged(m_volume);
}

void PlayerEngine::setMuted(bool m)
{
    if (m_muted == m) return;
    m_muted = m;
    emit mutedChanged(m_muted);
}

void PlayerEngine::open(const QUrl& url)
{
    LOG_INFO("PlayerEngine: open({})", url.toString().toStdString());
    stop();

    // 每次 open() 都从 PlaybackContext 实时取 finder，
    // 无需在构造时拷贝，shutdown() 后 clearFinder() 立即对所有实例生效。
    PlaybackContext& ctx = PlaybackContext::instance();
    if (!ctx.hasFinder()) {
        setError(QStringLiteral("PlayerEngine: no PluginFinder available — "
                                "PlayPlugin may not be initialized"));
        return;
    }

    m_plugin = ctx.findPlugin(url);
    if (!m_plugin) {
        setError(QStringLiteral("No plugin found for: ") + url.toString());
        return;
    }

    if (!m_plugin->open(url)) {
        setError(QStringLiteral("Plugin failed to open: ") + url.toString());
        return;
    }

    delete m_mediaInfo;
    m_mediaInfo = new MediaInfo(
        url,
        QFileInfo(url.toLocalFile()).baseName(),
        m_plugin->duration(),
        0, 0,
        url.fileName().section('.', -1),
        this
    );
    emit currentMediaChanged(m_mediaInfo);
    emit durationChanged(m_plugin->duration());
    play();
}

void PlayerEngine::play()
{
    if (!m_plugin) { LOG_WARN("PlayerEngine::play() — no plugin loaded"); return; }
    m_plugin->play();
    setState(Playing);
    m_positionTimer.start();
    LOG_INFO("PlayerEngine: play");
}

void PlayerEngine::pause()
{
    if (!m_plugin) return;
    m_plugin->pause();
    setState(Paused);
    m_positionTimer.stop();
    LOG_INFO("PlayerEngine: pause");
}

void PlayerEngine::stop()
{
    m_positionTimer.stop();
    if (m_plugin) { m_plugin->stop(); m_plugin = nullptr; }
    setState(Stopped);
    LOG_INFO("PlayerEngine: stop");
}

void PlayerEngine::seek(qint64 positionMs)
{
    if (!m_plugin) return;
    m_plugin->seek(positionMs);
    emit positionChanged(positionMs);
}

void PlayerEngine::togglePlayPause()
{
    if (m_state == Playing) pause(); else play();
}

void PlayerEngine::setState(PlaybackState s)
{
    if (m_state == s) return;
    m_state = s;
    emit playbackStateChanged(static_cast<int>(m_state));
}

void PlayerEngine::setError(const QString& msg)
{
    m_errorString = msg;
    LOG_ERROR("PlayerEngine error: {}", msg.toStdString());
    emit errorOccurred(msg);
}
