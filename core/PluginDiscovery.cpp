#include "PluginDiscovery.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace PluginBased::Plugins::PluginDiscovery {

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

QString metadataJsonPathForLibrary(const QString& filePath)
{
    const QFileInfo info(filePath);
    const QString pluginName = pluginNameFromLibraryFile(info.fileName());
    return info.dir().filePath(pluginName + QStringLiteral(".json"));
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

} // namespace PluginBased::Plugins::PluginDiscovery
