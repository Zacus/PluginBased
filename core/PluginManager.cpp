#include "PluginManager.h"
#include "Logger.h"

#include <QDir>
#include <QFileInfo>

void PluginManager::loadAll(const QString& pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        LOG_WARN("PluginManager: directory not found: {}", pluginDir.toStdString());
        return;
    }

#if defined(Q_OS_WIN)
    const QStringList filters{"*.dll"};
#elif defined(Q_OS_MAC)
    const QStringList filters{"*.dylib"};
#else
    const QStringList filters{"*.so"};
#endif

    const auto entries = dir.entryList(filters, QDir::Files);
    LOG_INFO("PluginManager: scanning {} — found {} files",
             pluginDir.toStdString(), entries.size());

    for (const QString& file : entries)
        loadPlugin(dir.absoluteFilePath(file));
}

bool PluginManager::loadPlugin(const QString& filePath)
{
    auto loader = std::make_unique<QPluginLoader>(filePath);
    QObject* obj = loader->instance();

    if (!obj) {
        const QString err = loader->errorString();
        LOG_ERROR("PluginManager: failed to load {} — {}",
                  filePath.toStdString(), err.toStdString());
        emit pluginLoadFailed(filePath, err);
        return false;
    }

    auto* plugin = qobject_cast<IPlayerPlugin*>(obj);
    if (!plugin) {
        LOG_ERROR("PluginManager: {} does not implement IPlayerPlugin",
                  filePath.toStdString());
        loader->unload();
        return false;
    }

    if (!plugin->initialize()) {
        LOG_ERROR("PluginManager: plugin {} initialize() failed",
                  plugin->name().toStdString());
        loader->unload();
        return false;
    }

    LOG_INFO("PluginManager: loaded plugin '{}' v{} — {}",
             plugin->name().toStdString(),
             plugin->version().toStdString(),
             plugin->description().toStdString());

    PluginEntry entry;
    entry.loader = std::move(loader);
    entry.plugin = plugin;
    m_plugins.push_back(std::move(entry));
    emit pluginsChanged();
    return true;
}

IPlayerPlugin* PluginManager::findPlugin(const QUrl& url) const
{
    for (const auto& entry : m_plugins) {
        if (entry.plugin && entry.plugin->canHandle(url))
            return entry.plugin;
    }
    LOG_WARN("PluginManager: no plugin found for {}", url.toString().toStdString());
    return nullptr;
}

QStringList PluginManager::pluginNames() const
{
    QStringList names;
    for (const auto& e : m_plugins)
        if (e.plugin) names << e.plugin->name();
    return names;
}

QString PluginManager::pluginVersion(const QString& name) const
{
    for (const auto& e : m_plugins)
        if (e.plugin && e.plugin->name() == name)
            return e.plugin->version();
    return {};
}

QString PluginManager::pluginDescription(const QString& name) const
{
    for (const auto& e : m_plugins)
        if (e.plugin && e.plugin->name() == name)
            return e.plugin->description();
    return {};
}
