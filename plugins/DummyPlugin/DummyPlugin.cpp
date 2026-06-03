/*
 * @Author: zs
 * @Date: 2026-03-25 19:18:22
 * @LastEditors: zs
 * @LastEditTime: 2026-03-27 20:28:55
 * @FilePath: /VideoPlayer/plugins/DummyPlugin/DummyPlugin.cpp
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

bool DummyPlugin::initialize(const PluginContext&)
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
