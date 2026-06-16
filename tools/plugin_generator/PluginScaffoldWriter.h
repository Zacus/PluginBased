#pragma once

// Writes rendered plugin scaffold files to disk.
// Rendering and validation stay in separate helpers so filesystem behavior remains focused.

#include "PluginGeneratorOptions.h"

#include <QVariantMap>
#include <QVector>

struct PluginScaffoldFile
{
    QString relativePath;
    QString text;
};

class PluginScaffoldWriter
{
public:
    QVariantMap writePlugin(const PluginGeneratorOptions& options,
                            const QVector<PluginScaffoldFile>& files) const;

private:
    static QVariantMap success(const QString& path);
    static QVariantMap failure(const QString& message);
    static bool writeTextFile(const QString& path, const QString& text, QString* error);
    bool copyIconAsset(const PluginGeneratorOptions& options,
                       const QString& pluginPath,
                       QString* error) const;
};
