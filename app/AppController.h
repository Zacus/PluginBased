#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>

class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString appVersion   READ appVersion   CONSTANT)
    Q_PROPERTY(QString appName      READ appName      CONSTANT)
    Q_PROPERTY(bool    pluginsReady READ pluginsReady NOTIFY pluginsReadyChanged)

public:
    static AppController* create(QQmlEngine* engine, QJSEngine* jsEngine);
    static AppController& instance();

    QString appVersion()   const { return QStringLiteral("1.0.0"); }
    QString appName()      const { return QStringLiteral("VideoPlayer"); }
    bool    pluginsReady() const { return m_pluginsReady; }

public slots:
    void initPlugins();
    void quit();
    Q_INVOKABLE void logInfo(const QString& msg);
    Q_INVOKABLE void logWarn(const QString& msg);
    Q_INVOKABLE void logError(const QString& msg);

signals:
    void pluginsReadyChanged();

private:
    explicit AppController(QObject* parent = nullptr);
    bool m_pluginsReady = false;
};
