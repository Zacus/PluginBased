#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QTimer>
#include <QQmlEngine>

#include "MediaInfo.h"
#include "IPlayerPlugin.h"

class PlayerEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int       playbackState READ playbackStateInt NOTIFY playbackStateChanged)
    Q_PROPERTY(qint64    position      READ position         NOTIFY positionChanged)
    Q_PROPERTY(qint64    duration      READ duration         NOTIFY durationChanged)
    Q_PROPERTY(float     volume        READ volume  WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool      muted         READ muted   WRITE setMuted  NOTIFY mutedChanged)
    Q_PROPERTY(MediaInfo* currentMedia READ currentMedia     NOTIFY currentMediaChanged)
    Q_PROPERTY(QString   errorString   READ errorString      NOTIFY errorOccurred)

public:
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

    IPlayerPlugin* m_plugin    = nullptr;
    MediaInfo*     m_mediaInfo = nullptr;
    PlaybackState  m_state     = Stopped;
    float          m_volume    = 1.0f;
    bool           m_muted     = false;
    QString        m_errorString;
    QTimer         m_positionTimer;
};
