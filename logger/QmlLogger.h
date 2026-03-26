#pragma once

#include <QObject>
#include <QQmlEngine>
#include "Logger.h"

/**
 * @brief QmlLogger —— 供 QML 层使用的日志单例
 *
 * 注册为独立 QML 模块 URI="AppLog" Version=1.0，
 * 在 QML 中以 "Log" 单例名访问：
 *
 *   import AppLog 1.0
 *   Log.info("hello")
 *   Log.warn("something wrong")
 *   Log.error("fatal: " + msg)
 *
 * ## 为什么独立成模块而非放在宿主的 VideoPlayer 1.0 里
 *
 * 日志是基础设施，与宿主业务（AppController、PluginManager）无关。
 * 独立模块名 "AppLog" 稳定，不随宿主业务重构变化，
 * 所有插件都可以 import AppLog 1.0 而无需知道宿主叫什么。
 *
 * ## 日志前缀
 * 调用方可在消息里自带前缀，例如：
 *   Log.info("[PlayPlugin] state changed")
 * QmlLogger 本身只做转发，不强加前缀。
 */
class QmlLogger : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_NAMED_ELEMENT(Log)      // QML 中以 "Log" 单例名访问，而非 "QmlLogger"
    QML_SINGLETON

public:
    static QmlLogger* create(QQmlEngine*, QJSEngine*)
    {
        auto& inst = instance();
        QQmlEngine::setObjectOwnership(&inst, QQmlEngine::CppOwnership);
        return &inst;
    }

    static QmlLogger& instance()
    {
        static QmlLogger s;
        return s;
    }

    Q_INVOKABLE void info(const QString& msg)
    {
        LOG_INFO("[QML] {}", msg.toStdString());
    }

    Q_INVOKABLE void warn(const QString& msg)
    {
        LOG_WARN("[QML] {}", msg.toStdString());
    }

    Q_INVOKABLE void error(const QString& msg)
    {
        LOG_ERROR("[QML] {}", msg.toStdString());
    }

    Q_INVOKABLE void debug(const QString& msg)
    {
        LOG_DEBUG("[QML] {}", msg.toStdString());
    }

private:
    explicit QmlLogger(QObject* parent = nullptr) : QObject(parent) {}
};
