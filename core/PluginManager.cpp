#include "PluginManager.h"
#include "Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

QStringList pluginLibraryFilters()
{
#if defined(Q_OS_WIN)
    return {"*.dll"};
#elif defined(Q_OS_MAC)
    // Qt MODULE 库在 macOS 上后缀是 .so，不是 .dylib
    return {"*.so"};
#else
    return {"*.so"};
#endif
}

QString manifestFilePath(const QDir& pluginDir)
{
    QDir rootDir(pluginDir);
    rootDir.cdUp();
    return rootDir.filePath(QStringLiteral("plugins.json"));
}

QString pluginNameFromLibraryFile(const QString& fileName)
{
    QString name = QFileInfo(fileName).completeBaseName();
    if (name.startsWith(QStringLiteral("lib")))
        name.remove(0, 3);
    return name;
}

QStringList manifestPluginNames(const QString& manifestPath, QString* error)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("PluginManager: plugin manifest not found: %1")
                         .arg(manifestPath);
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("PluginManager: invalid plugin manifest %1: %2")
                         .arg(manifestPath, parseError.errorString());
        return {};
    }

    if (!document.isObject() || !document.object().value(QStringLiteral("plugins")).isArray()) {
        if (error)
            *error = QStringLiteral("PluginManager: invalid plugin manifest %1: missing plugins array")
                         .arg(manifestPath);
        return {};
    }

    QStringList names;
    const QJsonArray plugins = document.object().value(QStringLiteral("plugins")).toArray();
    for (qsizetype i = 0; i < plugins.size(); ++i) {
        const QJsonValue value = plugins.at(i);
        if (!value.isString()) {
            if (error)
                *error = QStringLiteral("PluginManager: invalid plugin manifest entry at index %1")
                             .arg(i);
            return {};
        }

        const QString name = value.toString();
        if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(QStringLiteral(".."))) {
            if (error)
                *error = QStringLiteral("PluginManager: invalid plugin name in manifest: '%1'")
                             .arg(name);
            return {};
        }
        if (!names.contains(name))
            names << name;
    }

    return names;
}

QString findPluginLibraryFile(const QDir& dir, const QString& pluginName, const QStringList& entries)
{
    for (const QString& file : entries) {
        if (pluginNameFromLibraryFile(file) == pluginName)
            return dir.absoluteFilePath(file);
    }
    return {};
}

} // namespace

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

    QString manifestError;
    const QStringList enabledPlugins = manifestPluginNames(manifestFilePath(dir),
                                                           &manifestError);
    if (!manifestError.isEmpty()) {
        LOG_ERROR("{}", manifestError.toStdString());
        return;
    }

    const QStringList filters = pluginLibraryFilters();
    const auto entries = dir.entryList(filters, QDir::Files);
    LOG_INFO("PluginManager: scanning {} — found {} files, manifest lists {} plugins",
             pluginDir.toStdString(), entries.size(), enabledPlugins.size());

    for (const QString& pluginName : enabledPlugins) {
        const QString filePath = findPluginLibraryFile(dir, pluginName, entries);
        if (filePath.isEmpty()) {
            LOG_WARN("PluginManager: plugin '{}' listed in manifest but no library was found",
                     pluginName.toStdString());
            continue;
        }
        loadPlugin(filePath);
    }
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

    auto* plugin = qobject_cast<IAppPlugin*>(obj);
    if (!plugin) {
        const QString err = QStringLiteral("%1 does not implement IAppPlugin")
                                .arg(filePath);
        LOG_ERROR("PluginManager: {}", err.toStdString());
        loader->unload();
        emit pluginLoadFailed(filePath, err);
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

QUrl PluginManager::pluginCardIconUrl(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return {};
    return m_plugins[index].plugin ? m_plugins[index].plugin->cardIconUrl() : QUrl{};
}

QString PluginManager::pluginCardName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_plugins.size())) return {};
    return m_plugins[index].plugin ? m_plugins[index].plugin->cardName() : QString();
}
