#pragma once

#include <QDir>
#include <QString>
#include <QStringList>

namespace PluginBased::Plugins::PluginDiscovery {

QStringList pluginLibraryFilters();
QString manifestFilePath(const QDir& pluginDir);
QString pluginNameFromLibraryFile(const QString& fileName);
QString metadataJsonPathForLibrary(const QString& filePath);
QStringList manifestPluginNames(const QString& manifestPath, QString* error);
QString findPluginLibraryFile(const QDir& dir, const QString& pluginName, const QStringList& entries);

} // namespace PluginBased::Plugins::PluginDiscovery
