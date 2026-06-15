/*
 * @Author: zs
 * @Date: 2026-03-25 19:18:22
 * @LastEditors: zs
 * @LastEditTime: 2026-03-27 20:28:55
 * @FilePath: /PluginBased/plugins/DummyPlugin/DummyPlugin.cpp
 * @Description: 
 * 
 * Copyright (c) 2026 by zs, All Rights Reserved. 
 */
#include "DummyPlugin.h"
#include "Logger.h"

DummyPlugin::DummyPlugin(QObject* parent)
    : QObject(parent)
{}

DummyPlugin::~DummyPlugin()
{
    shutdown();
}

bool DummyPlugin::initialize()
{
    LOG_INFO("DummyPlugin::initialize()");
    return true;
}

void DummyPlugin::shutdown()
{
    LOG_INFO("DummyPlugin::shutdown()");
}

QUrl DummyPlugin::qmlComponentUrl() const
{
    return QUrl(QStringLiteral("qrc:/DummyPlugin/qml/DummyPluginView.qml"));
}

QStringList DummyPlugin::translationResourcePaths(const QString& languageName) const
{
    if (languageName == QStringLiteral("zh_CN"))
        return { QStringLiteral(":/DummyPlugin/i18n/DummyPlugin_zh_CN.qm") };

    return {};
}
