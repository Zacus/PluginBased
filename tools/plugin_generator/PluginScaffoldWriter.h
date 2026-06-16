#pragma once

// 将渲染后的插件脚手架文件写入磁盘。
// 渲染和校验由其他辅助类负责，本类只关注文件系统行为。

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
