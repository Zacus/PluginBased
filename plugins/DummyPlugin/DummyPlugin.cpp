#include "DummyPlugin.h"
#include "Logger.h"

DummyPlugin::DummyPlugin(QObject* parent)
    : QObject(parent)
{}

DummyPlugin::~DummyPlugin()
{
    shutdown();
}

bool DummyPlugin::initialize()
{
    LOG_INFO("DummyPlugin::initialize()");
    return true;
}

void DummyPlugin::shutdown()
{
    LOG_INFO("DummyPlugin::shutdown()");
    m_playing = false;
}

bool DummyPlugin::canHandle(const QUrl& url) const
{
    // Dummy 插件接受所有本地文件（演示用）
    return url.isLocalFile();
}

bool DummyPlugin::open(const QUrl& url)
{
    m_url      = url;
    m_position = 0;
    m_playing  = false;
    LOG_INFO("DummyPlugin::open({})", url.toString().toStdString());
    return true;
}

void DummyPlugin::play()
{
    m_playing = true;
    LOG_INFO("DummyPlugin::play()");
}

void DummyPlugin::pause()
{
    m_playing = false;
    LOG_INFO("DummyPlugin::pause()");
}

void DummyPlugin::stop()
{
    m_playing  = false;
    m_position = 0;
    LOG_INFO("DummyPlugin::stop()");
}

void DummyPlugin::seek(qint64 positionMs)
{
    m_position = qBound(0LL, positionMs, m_duration);
    LOG_DEBUG("DummyPlugin::seek({}ms)", positionMs);
}
