#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

/**
 * @brief 媒体信息（只读，由 PlayerEngine 创建）
 *
 * 通过 qmlRegisterUncreatableType 注册，QML 中无法 new，
 * 只能读取 PlayerEngine.currentMedia 属性。
 */
class MediaInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl     url       READ url       CONSTANT)
    Q_PROPERTY(QString  title     READ title     CONSTANT)
    Q_PROPERTY(qint64   duration  READ duration  CONSTANT)
    Q_PROPERTY(int      width     READ width     CONSTANT)
    Q_PROPERTY(int      height    READ height    CONSTANT)
    Q_PROPERTY(QString  format    READ format    CONSTANT)

public:
    explicit MediaInfo(QObject* parent = nullptr) : QObject(parent) {}

    MediaInfo(const QUrl& url,
              const QString& title,
              qint64 duration,
              int width, int height,
              const QString& format,
              QObject* parent = nullptr)
        : QObject(parent)
        , m_url(url), m_title(title), m_duration(duration)
        , m_width(width), m_height(height), m_format(format)
    {}

    QUrl    url()      const { return m_url; }
    QString title()    const { return m_title; }
    qint64  duration() const { return m_duration; }
    int     width()    const { return m_width; }
    int     height()   const { return m_height; }
    QString format()   const { return m_format; }

    void setUrl(const QUrl& v)      { m_url      = v; }
    void setTitle(const QString& v) { m_title    = v; }
    void setDuration(qint64 v)      { m_duration = v; }
    void setSize(int w, int h)      { m_width = w; m_height = h; }
    void setFormat(const QString& v){ m_format   = v; }

private:
    QUrl    m_url;
    QString m_title;
    qint64  m_duration = 0;
    int     m_width    = 0;
    int     m_height   = 0;
    QString m_format;
};
