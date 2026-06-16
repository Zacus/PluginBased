#pragma once

// Renders generated plugin file text without touching the filesystem.
// PluginTemplateGenerator owns validation and PluginScaffoldWriter owns disk writes.

#include "PluginGeneratorOptions.h"

#include <QString>

class PluginTemplateRenderer
{
public:
    QString headerText(const PluginGeneratorOptions& options) const;
    QString sourceText(const PluginGeneratorOptions& options) const;
    QString metadataText(const PluginGeneratorOptions& options) const;
    QString cmakeText(const PluginGeneratorOptions& options) const;
    QString qmlText(const PluginGeneratorOptions& options) const;
    QString translationText(const PluginGeneratorOptions& options) const;

    static QString cppString(const QString& value);
    static QString jsonString(const QString& value);
    static QString qmlString(const QString& value);
    static QString xmlString(const QString& value);
};
