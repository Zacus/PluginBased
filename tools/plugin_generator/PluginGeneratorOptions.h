#pragma once

// Shared value type for parsed plugin generator options.
// It lets renderer and writer helpers exchange generator state without depending on QObject.

#include <QString>

struct PluginGeneratorOptions
{
    QString pluginName;
    QString pluginId;
    QString displayName;
    QString description;
    QString icon;
    QString iconPath;
    QString iconAssetName;
    QString outputDir;
    bool withQml = true;
};
