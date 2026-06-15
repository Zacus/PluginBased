#pragma once

#include <QObject>
#include <QVariantMap>
#include <QString>
#include <QtQml/qqmlregistration.h>

class PluginTemplateGenerator : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit PluginTemplateGenerator(QObject* parent = nullptr);

    Q_INVOKABLE QVariantMap generate(const QVariantMap& options);
    Q_INVOKABLE QString defaultOutputDir() const;

private:
    struct Options
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

    static bool isValidPluginName(const QString& name);
    static QString toPluginId(const QString& name);
    static QString cppString(const QString& value);
    static QString jsonString(const QString& value);
    static QString qmlString(const QString& value);
    static QString xmlString(const QString& value);
    static QVariantMap success(const QString& path);
    static QVariantMap failure(const QString& message);

    QVariantMap parseOptions(const QVariantMap& input, Options* options) const;
    QVariantMap writePlugin(const Options& options) const;

    QString headerText(const Options& options) const;
    QString sourceText(const Options& options) const;
    QString metadataText(const Options& options) const;
    QString cmakeText(const Options& options) const;
    QString qmlText(const Options& options) const;
    QString translationText(const Options& options) const;
    bool copyIconAsset(const Options& options, const QString& pluginPath, QString* error) const;
};
