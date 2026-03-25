#include "PluginManager.h"
#include "Logger.h"

#include <QDir>
#include <QFileInfo>

void PluginManager::unloadAll()
{
    // 逆序 shutdown，再逆序 unload，保证依赖顺序
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
        if (it->plugin) it->plugin->shutdown();

    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
        if (it->loader) it->loader->unload();

    m_plugins.clear();
    LOG_INFO("PluginManager: all plugins unloaded");
}

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
    // Qt MODULE 库在 macOS 上后缀是 .so，不是 .dylib
    const QStringList filters{"*.so"};
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

    // 构造 PluginFinder 并注入：插件通过此 lambda 查找其他已加载插件，
    // 无需 #include PluginManager.h，彻底断开对宿主层的静态依赖。
    IPlayerPlugin::PluginFinder finder = [this](const QUrl& url) -> IPlayerPlugin* {
        return this->findPlugin(url);
    };

    if (!plugin->initialize(finder)) {
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

QString PluginManager::pluginName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return {};
    return m_plugins[index].plugin ? m_plugins[index].plugin->name() : QString();
}

QString PluginManager::pluginDescriptionAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return {};
    return m_plugins[index].plugin ? m_plugins[index].plugin->description() : QString();
}

bool PluginManager::pluginHasQmlUI(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return false;
    return m_plugins[index].plugin ? m_plugins[index].plugin->hasQmlUI() : false;
}

QUrl PluginManager::pluginQmlUrl(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return {};
    return m_plugins[index].plugin ? m_plugins[index].plugin->qmlComponentUrl() : QUrl{};
}

QString PluginManager::pluginCardIcon(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return QStringLiteral("⬡");
    return m_plugins[index].plugin ? m_plugins[index].plugin->cardIcon() : QStringLiteral("⬡");
}

QString PluginManager::pluginCardName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return {};
    return m_plugins[index].plugin ? m_plugins[index].plugin->cardName() : QString();
}
