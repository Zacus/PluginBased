#pragma once

// 插件生成器解析后的共享参数值类型。
// 渲染器和写入器通过它交换状态，避免依赖 QObject。

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
