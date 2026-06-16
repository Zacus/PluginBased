#pragma once

// 渲染插件脚手架的各类文件内容，不直接访问文件系统。
// 参数校验由 PluginTemplateGenerator 负责，落盘由 PluginScaffoldWriter 负责。

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
