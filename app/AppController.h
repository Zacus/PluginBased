#pragma once

#include <QObject>
#include <QString>
#include <QQmlEngine>

class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString appVersion   READ appVersion   CONSTANT)
    Q_PROPERTY(QString appName      READ appName      CONSTANT)
    Q_PROPERTY(bool    pluginsReady READ pluginsReady NOTIFY pluginsReadyChanged)
    Q_PROPERTY(QString currentTheme READ currentTheme NOTIFY currentThemeChanged)

public:
    // QML_SINGLETON 要求提供此静态工厂函数
    static AppController* create(QQmlEngine*, QJSEngine*)
    {
        QQmlEngine::setObjectOwnership(&instance(), QQmlEngine::CppOwnership);
        return &instance();
    }

    static AppController& instance()
    {
        static AppController s;
        return s;
    }

    QString appVersion()   const { return QStringLiteral("1.0.0"); }
    QString appName()      const { return QStringLiteral("PluginBased"); }
    bool    pluginsReady() const { return m_pluginsReady; }
    QString currentTheme() const { return m_currentTheme; }

public slots:
    void initPlugins();
    void quit();
    Q_INVOKABLE void setTheme(const QString& themeName);
    Q_INVOKABLE void toggleTheme();
    Q_INVOKABLE void logInfo(const QString& msg);
    Q_INVOKABLE void logWarn(const QString& msg);
    Q_INVOKABLE void logError(const QString& msg);
    /** 热重载配置文件（运行时修改 INI 后调用，无需重启即可生效） */
    Q_INVOKABLE void reloadConfig();

signals:
    void pluginsReadyChanged();
    void currentThemeChanged();

private:
    explicit AppController(QObject* parent = nullptr) : QObject(parent) {}
    bool m_pluginsReady = false;
    QString m_currentTheme { "dark" };
};
