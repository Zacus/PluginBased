#pragma once

#include <QJsonDocument>
#include <QString>

namespace PluginBased::Plugins {

struct PluginMetadata
{
    QString iid;
    int schemaVersion = 0;
    int apiVersion = 0;
    int abiVersion = 0;
    QString id;
    QString name;
    QString version;
    QString description;
    bool hasQml = false;
};

struct PluginMetadataValidationResult
{
    bool ok = false;
    PluginMetadata metadata;
    QString error;
};

class PluginMetadataValidator
{
public:
    static PluginMetadataValidationResult validateFile(const QString& metadataPath,
                                                       const QString& expectedPluginName);
    static PluginMetadataValidationResult validateDocument(const QJsonDocument& document,
                                                           const QString& expectedPluginName,
                                                           const QString& sourceName);
    static QString runtimeConsistencyError(const PluginMetadata& metadata,
                                           const QString& actualId,
                                           const QString& actualName,
                                           const QString& actualVersion,
                                           bool actualHasQml);
};

} // namespace PluginBased::Plugins
