#pragma once

#include <QUrl>
#include <QtPlugin>

#define IPlayerPlugin_IID "com.videoplayer.IPlayerPlugin/1.0"

class IPlayerPlugin
{
public:
    virtual ~IPlayerPlugin() = default;

    virtual bool canHandle(const QUrl& url) const = 0;

    virtual bool open(const QUrl& url) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 positionMs) = 0;

    virtual qint64 duration() const = 0;
    virtual qint64 position() const = 0;
    virtual bool isPlaying() const = 0;
};

Q_DECLARE_INTERFACE(IPlayerPlugin, IPlayerPlugin_IID)
