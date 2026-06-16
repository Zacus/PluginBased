#include "PluginScaffoldWriter.h"

// 实现插件脚手架的文件系统写入。
// 若目标目录已存在则拒绝生成，避免覆盖用户已有文件。

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

QVariantMap PluginScaffoldWriter::writePlugin(const PluginGeneratorOptions& options,
                                              const QVector<PluginScaffoldFile>& files) const
{
    QDir outputParent(options.outputDir);
    if (!outputParent.exists() && !outputParent.mkpath(QStringLiteral("."))) {
        return failure(QStringLiteral("Failed to create output directory: %1")
                           .arg(QDir::cleanPath(options.outputDir)));
    }

    const QString pluginPath = outputParent.absoluteFilePath(options.pluginName);
    if (QFileInfo::exists(pluginPath))
        return failure(QStringLiteral("Plugin directory already exists: %1").arg(pluginPath));

    QDir pluginDir;
    if (!pluginDir.mkpath(pluginPath))
        return failure(QStringLiteral("Failed to create plugin directory: %1").arg(pluginPath));

    if (options.withQml && !pluginDir.mkpath(pluginPath + QStringLiteral("/qml")))
        return failure(QStringLiteral("Failed to create qml directory: %1/qml").arg(pluginPath));
    if (!pluginDir.mkpath(pluginPath + QStringLiteral("/translations")))
        return failure(QStringLiteral("Failed to create translations directory: %1/translations").arg(pluginPath));
    if (!options.iconPath.isEmpty() && !pluginDir.mkpath(pluginPath + QStringLiteral("/assets")))
        return failure(QStringLiteral("Failed to create assets directory: %1/assets").arg(pluginPath));

    QString error;
    for (const PluginScaffoldFile& file : files) {
        if (!writeTextFile(pluginPath + QStringLiteral("/") + file.relativePath, file.text, &error))
            return failure(error);
    }

    if (!copyIconAsset(options, pluginPath, &error))
        return failure(error);

    return success(QDir::cleanPath(pluginPath));
}

QVariantMap PluginScaffoldWriter::success(const QString& path)
{
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), path},
        {QStringLiteral("message"), QStringLiteral("插件已生成")},
    };
}

QVariantMap PluginScaffoldWriter::failure(const QString& message)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), message},
    };
}

bool PluginScaffoldWriter::writeTextFile(const QString& path, const QString& text, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("Failed to write %1: %2").arg(path, file.errorString());
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    return true;
}

bool PluginScaffoldWriter::copyIconAsset(const PluginGeneratorOptions& options,
                                         const QString& pluginPath,
                                         QString* error) const
{
    if (options.iconPath.isEmpty())
        return true;

    const QString targetPath = pluginPath + QStringLiteral("/assets/") + options.iconAssetName;
    if (!QFile::copy(options.iconPath, targetPath)) {
        *error = QStringLiteral("Failed to copy icon to %1").arg(targetPath);
        return false;
    }
    return true;
}
