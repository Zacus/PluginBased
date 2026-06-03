#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>

struct PluginContext
{};

#define IAppPlugin_IID "com.videoplayer.IAppPlugin/1.0"

class IAppPlugin
{
public:
    virtual ~IAppPlugin() = default;

    virtual QString id()          const = 0;
    virtual QString name()        const = 0;
    virtual QString version()     const = 0;
    virtual QString description() const = 0;

    virtual bool initialize(const PluginContext& context) = 0;
    virtual void shutdown() = 0;

    virtual bool hasQmlUI() const { return false; }
    virtual QUrl qmlComponentUrl() const { return QUrl{}; }
    virtual QString cardIcon() const { return QStringLiteral("⬡"); }
    virtual QString cardName() const { return name(); }
};

Q_DECLARE_INTERFACE(IAppPlugin, IAppPlugin_IID)
