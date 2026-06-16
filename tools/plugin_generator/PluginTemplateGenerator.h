#pragma once

// 暴露给 QML 的插件模板生成服务。
// 负责参数解析和生成流程编排，具体模板渲染与文件写入由辅助类完成。

#include "PluginGeneratorOptions.h"

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
    static bool isValidPluginName(const QString& name);
    static QString toPluginId(const QString& name);
    static QVariantMap success(const QString& path);
    static QVariantMap failure(const QString& message);

    QVariantMap parseOptions(const QVariantMap& input, PluginGeneratorOptions* options) const;
};
