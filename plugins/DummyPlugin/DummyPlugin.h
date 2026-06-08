/*
 * @Author: zs
 * @Date: 2026-03-25 19:18:16
 * @LastEditors: zs
 * @LastEditTime: 2026-03-27 20:27:55
 * @FilePath: /PluginBased/plugins/DummyPlugin/DummyPlugin.h
 * @Description: 
 * 
 * Copyright (c) 2026 by zs, All Rights Reserved. 
 */
#pragma once

#include <QObject>
#include "IAppPlugin.h"

/**
 * @brief 示例插件 —— 不做真实解码，仅验证插件框架运转
 *
 * 生产插件（如 FFmpeg 插件）照此接口实现即可。
 */
class DummyPlugin : public QObject, public IAppPlugin
{
    Q_OBJECT
    Q_INTERFACES(IAppPlugin)
    Q_PLUGIN_METADATA(IID IAppPlugin_IID FILE "DummyPlugin.json")

public:
    explicit DummyPlugin(QObject* parent = nullptr);
    ~DummyPlugin() override;

    // ── 元信息 ────────────────────────────────────────────────────────────
    QString id()          const override { return QStringLiteral("dummy"); }
    QString name()        const override { return QStringLiteral("DummyPlugin"); }
    QString version()     const override { return QStringLiteral("1.0.0"); }
    QString description() const override { return QStringLiteral("Stub plugin for framework validation"); }
    QString cardIcon()    const override { return QStringLiteral("⬡"); }
    QString cardName()    const override { return QStringLiteral("示例插件"); }

    // ── 生命周期 ──────────────────────────────────────────────────────────
    bool initialize() override;
    void shutdown()   override;

    bool hasQmlUI()        const override { return true; }
    QUrl qmlComponentUrl() const override;
};
