#include "PluginMetadataValidator.h"

#include "IAppPlugin.h"

#include <QFile>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

namespace PluginBased::Plugins {

namespace {

PluginMetadataValidationResult failure(const QString& sourceName, const QString& message)
{
    PluginMetadataValidationResult result;
    result.error = QStringLiteral("%1: %2").arg(sourceName, message);
    return result;
}

PluginMetadataValidationResult success(const PluginMetadata& metadata)
{
    PluginMetadataValidationResult result;
    result.ok = true;
    result.metadata = metadata;
    return result;
}

bool readRequiredInt(const QJsonObject& object,
                     const QString& field,
                     const QString& sourceName,
                     int* out,
                     PluginMetadataValidationResult* error)
{
    const QJsonValue value = object.value(field);
    if (!value.isDouble()) {
        *error = failure(sourceName, QStringLiteral("MetaData.%1 must be an integer").arg(field));
        return false;
    }

    const double number = value.toDouble();
    const int integer = value.toInt();
    if (number != static_cast<double>(integer)) {
        *error = failure(sourceName, QStringLiteral("MetaData.%1 must be an integer").arg(field));
        return false;
    }

    *out = integer;
    return true;
}

bool readRequiredString(const QJsonObject& object,
                        const QString& field,
                        const QString& sourceName,
                        QString* out,
                        PluginMetadataValidationResult* error)
{
    const QJsonValue value = object.value(field);
    if (!value.isString()) {
        *error = failure(sourceName, QStringLiteral("MetaData.%1 must be a string").arg(field));
        return false;
    }

    *out = value.toString();
    return true;
}

bool readRequiredBool(const QJsonObject& object,
                      const QString& field,
                      const QString& sourceName,
                      bool* out,
                      PluginMetadataValidationResult* error)
{
    const QJsonValue value = object.value(field);
    if (!value.isBool()) {
        *error = failure(sourceName, QStringLiteral("MetaData.%1 must be a bool").arg(field));
        return false;
    }

    *out = value.toBool();
    return true;
}

bool matchesPattern(const QString& value, const QString& pattern)
{
    const QRegularExpression regex(pattern);
    return regex.match(value).hasMatch();
}

QString metadataMismatchError(const QString& field,
                              const QString& expected,
                              const QString& actual)
{
    return QStringLiteral("metadata %1 expected %2, got %3")
        .arg(field, expected, actual);
}

} // namespace

PluginMetadataValidationResult PluginMetadataValidator::validateFile(const QString& metadataPath,
                                                                     const QString& expectedPluginName)
{
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return failure(metadataPath, QStringLiteral("failed to open metadata: %1").arg(file.errorString()));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return failure(metadataPath,
                       QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    }

    return validateDocument(document, expectedPluginName, metadataPath);
}

PluginMetadataValidationResult PluginMetadataValidator::validateDocument(const QJsonDocument& document,
                                                                         const QString& expectedPluginName,
                                                                         const QString& sourceName)
{
    if (!document.isObject())
        return failure(sourceName, QStringLiteral("metadata root must be an object"));

    const QJsonObject root = document.object();
    const QJsonValue iidValue = root.value(QStringLiteral("IID"));
    if (!iidValue.isString())
        return failure(sourceName, QStringLiteral("IID must be a string"));

    PluginMetadata metadata;
    metadata.iid = iidValue.toString();
    if (metadata.iid != QStringLiteral(IAppPlugin_IID)) {
        return failure(sourceName,
                       QStringLiteral("IID expected %1, got %2")
                           .arg(QStringLiteral(IAppPlugin_IID), metadata.iid));
    }

    const QJsonValue metadataValue = root.value(QStringLiteral("MetaData"));
    if (!metadataValue.isObject())
        return failure(sourceName, QStringLiteral("MetaData must be an object"));

    const QJsonObject object = metadataValue.toObject();
    PluginMetadataValidationResult error;
    if (!readRequiredInt(object, QStringLiteral("schemaVersion"), sourceName, &metadata.schemaVersion, &error))
        return error;
    if (!readRequiredInt(object, QStringLiteral("apiVersion"), sourceName, &metadata.apiVersion, &error))
        return error;
    if (!readRequiredInt(object, QStringLiteral("abiVersion"), sourceName, &metadata.abiVersion, &error))
        return error;
    if (!readRequiredString(object, QStringLiteral("id"), sourceName, &metadata.id, &error))
        return error;
    if (!readRequiredString(object, QStringLiteral("name"), sourceName, &metadata.name, &error))
        return error;
    if (!readRequiredString(object, QStringLiteral("version"), sourceName, &metadata.version, &error))
        return error;
    if (!readRequiredString(object, QStringLiteral("description"), sourceName, &metadata.description, &error))
        return error;
    if (!readRequiredBool(object, QStringLiteral("hasQml"), sourceName, &metadata.hasQml, &error))
        return error;

    if (metadata.schemaVersion != 1) {
        return failure(sourceName,
                       QStringLiteral("MetaData.schemaVersion expected 1, got %1")
                           .arg(metadata.schemaVersion));
    }
    if (metadata.apiVersion != PluginBasedPluginApiVersion) {
        return failure(sourceName,
                       QStringLiteral("MetaData.apiVersion expected %1, got %2")
                           .arg(PluginBasedPluginApiVersion)
                           .arg(metadata.apiVersion));
    }
    if (metadata.abiVersion != PluginBasedPluginAbiVersion) {
        return failure(sourceName,
                       QStringLiteral("MetaData.abiVersion expected %1, got %2")
                           .arg(PluginBasedPluginAbiVersion)
                           .arg(metadata.abiVersion));
    }
    if (!matchesPattern(metadata.id, QStringLiteral("^[a-z][a-z0-9-]*$"))) {
        return failure(sourceName,
                       QStringLiteral("MetaData.id must match ^[a-z][a-z0-9-]*$"));
    }
    if (metadata.name != expectedPluginName) {
        return failure(sourceName,
                       QStringLiteral("MetaData.name expected %1, got %2")
                           .arg(expectedPluginName, metadata.name));
    }
    if (!matchesPattern(metadata.version, QStringLiteral("^\\d+\\.\\d+\\.\\d+$"))) {
        return failure(sourceName,
                       QStringLiteral("MetaData.version must match x.y.z"));
    }

    return success(metadata);
}

QString PluginMetadataValidator::runtimeConsistencyError(const PluginMetadata& metadata,
                                                         const QString& actualId,
                                                         const QString& actualName,
                                                         const QString& actualVersion,
                                                         bool actualHasQml)
{
    if (actualId != metadata.id) {
        return metadataMismatchError(QStringLiteral("id"),
                                     metadata.id,
                                     actualId);
    }
    if (actualName != metadata.name) {
        return metadataMismatchError(QStringLiteral("name"),
                                     metadata.name,
                                     actualName);
    }
    if (actualVersion != metadata.version) {
        return metadataMismatchError(QStringLiteral("version"),
                                     metadata.version,
                                     actualVersion);
    }
    if (actualHasQml != metadata.hasQml) {
        return metadataMismatchError(QStringLiteral("hasQml"),
                                     metadata.hasQml ? QStringLiteral("true") : QStringLiteral("false"),
                                     actualHasQml ? QStringLiteral("true") : QStringLiteral("false"));
    }

    return {};
}

} // namespace PluginBased::Plugins
