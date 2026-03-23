#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QPluginLoader>
#include <memory>
#include <vector>

#include "IPlayerPlugin.h"

class PluginManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int         pluginCount READ pluginCount NOTIFY pluginsChanged)
    Q_PROPERTY(QStringList pluginNames READ pluginNames NOTIFY pluginsChanged)

public:
    static PluginManager& instance();
    static PluginManager* create(QQmlEngine* engine, QJSEngine* jsEngine);

    void loadAll(const QString& pluginDir);
    bool loadPlugin(const QString& filePath);
    IPlayerPlugin* findPlugin(const QUrl& url) const;

    int         pluginCount() const { return static_cast<int>(m_plugins.size()); }
    QStringList pluginNames() const;

    Q_INVOKABLE QString pluginVersion(const QString& name) const;
    Q_INVOKABLE QString pluginDescription(const QString& name) const;

signals:
    void pluginsChanged();
    void pluginLoadFailed(const QString& path, const QString& reason);

private:
    explicit PluginManager(QObject* parent = nullptr);

    struct PluginEntry {
        std::unique_ptr<QPluginLoader> loader;
        IPlayerPlugin*                 plugin = nullptr;
        PluginEntry() = default;
        PluginEntry(PluginEntry&&) = default;
        PluginEntry& operator=(PluginEntry&&) = default;
        PluginEntry(const PluginEntry&) = delete;
        PluginEntry& operator=(const PluginEntry&) = delete;
    };

    std::vector<PluginEntry> m_plugins;
};
