#include "PluginManager.h"
#include "Logger.h"
#include "PluginDiscovery.h"
#include "PluginMetadataValidator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

using PluginBased::Plugins::PluginMetadata;
using PluginBased::Plugins::PluginMetadataValidationResult;
using PluginBased::Plugins::PluginMetadataValidator;
namespace PluginDiscovery = PluginBased::Plugins::PluginDiscovery;

void PluginManager::unloadAll()
{
    removeInstalledTranslators();

    // 逆序 shutdown，再逆序 unload，保证依赖顺序
    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
        if (it->plugin) it->plugin->shutdown();

    for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it)
        if (it->loader) it->loader->unload();

    m_plugins.clear();
    LOG_INFO("PluginManager: all plugins unloaded");
}

void PluginManager::applyLanguage(const QString& languageName)
{
    removeInstalledTranslators();

    for (const auto& entry : m_plugins) {
        if (!entry.plugin)
            continue;

        const QStringList resourcePaths = entry.plugin->translationResourcePaths(languageName);
        for (const QString& resourcePath : resourcePaths) {
            auto translator = std::make_unique<QTranslator>();
            if (!translator->load(resourcePath)) {
                LOG_WARN("PluginManager: failed to load plugin translation '{}'",
                         resourcePath.toStdString());
                continue;
            }

            if (!QCoreApplication::installTranslator(translator.get())) {
                LOG_WARN("PluginManager: failed to install plugin translation '{}'",
                         resourcePath.toStdString());
                continue;
            }

            LOG_INFO("PluginManager: installed plugin translation '{}'",
                     resourcePath.toStdString());
            m_pluginTranslators.push_back(std::move(translator));
        }
    }
}

void PluginManager::removeInstalledTranslators()
{
    for (auto it = m_pluginTranslators.rbegin(); it != m_pluginTranslators.rend(); ++it) {
        if (*it)
            QCoreApplication::removeTranslator(it->get());
    }
    m_pluginTranslators.clear();
}

void PluginManager::loadAll(const QString& pluginDir)
{
    QDir dir(pluginDir);
    if (!dir.exists()) {
        LOG_WARN("PluginManager: directory not found: {}", pluginDir.toStdString());
        return;
    }

    QString manifestError;
    const QStringList enabledPlugins =
        PluginDiscovery::manifestPluginNames(PluginDiscovery::manifestFilePath(dir),
                                             &manifestError);
    if (!manifestError.isEmpty()) {
        LOG_ERROR("{}", manifestError.toStdString());
        return;
    }

    const QStringList filters = PluginDiscovery::pluginLibraryFilters();
    const auto entries = dir.entryList(filters, QDir::Files);
    LOG_INFO("PluginManager: scanning {} — found {} files, manifest lists {} plugins",
             pluginDir.toStdString(), entries.size(), enabledPlugins.size());

    for (const QString& pluginName : enabledPlugins) {
        const QString filePath = PluginDiscovery::findPluginLibraryFile(dir, pluginName, entries);
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
    const QString metadataPath = PluginDiscovery::metadataJsonPathForLibrary(filePath);
    const QString expectedPluginName =
        PluginDiscovery::pluginNameFromLibraryFile(QFileInfo(filePath).fileName());
    const PluginMetadataValidationResult metadataResult =
        PluginMetadataValidator::validateFile(metadataPath, expectedPluginName);
    if (!metadataResult.ok) {
        const QString err = QStringLiteral("PluginManager: rejected plugin %1 metadata %2")
                                .arg(expectedPluginName, metadataResult.error);
        LOG_ERROR("{}", err.toStdString());
        emit pluginLoadFailed(metadataPath, err);
        return false;
    }

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

    const PluginMetadata& metadata = metadataResult.metadata;
    const QString consistencyError =
        PluginMetadataValidator::runtimeConsistencyError(metadata,
                                                         plugin->id(),
                                                         plugin->name(),
                                                         plugin->version(),
                                                         plugin->hasQmlUI());

    if (!consistencyError.isEmpty()) {
        const QString err = QStringLiteral("%1 failed metadata consistency check: %2")
                                .arg(filePath, consistencyError);
        LOG_ERROR("PluginManager: {}", err.toStdString());
        loader->unload();
        emit pluginLoadFailed(filePath, err);
        return false;
    }

    if (!plugin->initialize()) {
        LOG_ERROR("PluginManager: plugin {} initialize() failed",
                  plugin->name().toStdString());
        emit pluginLoadFailed(filePath,
                              QStringLiteral("%1 initialize() failed").arg(plugin->name()));
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
