#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QPluginLoader>
#include <memory>
#include <vector>

#include "IAppPlugin.h"

class PluginManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int         pluginCount READ pluginCount NOTIFY pluginsChanged)
    Q_PROPERTY(QStringList pluginNames READ pluginNames NOTIFY pluginsChanged)

public:
    static PluginManager* create(QQmlEngine*, QJSEngine*)
    {
        QQmlEngine::setObjectOwnership(&instance(), QQmlEngine::CppOwnership);
        return &instance();
    }

    static PluginManager& instance()
    {
        static PluginManager s;
        return s;
    }

    void loadAll(const QString& pluginDir);
    bool loadPlugin(const QString& filePath);
    void unloadAll();

    int         pluginCount() const { return static_cast<int>(m_plugins.size()); }
    QStringList pluginNames() const;

    Q_INVOKABLE QString pluginVersion(const QString& name) const;
    Q_INVOKABLE QString pluginDescription(const QString& name) const;

    // 按索引访问（QML HomePanel 使用）
    Q_INVOKABLE QString pluginName(int index) const;
    Q_INVOKABLE QString pluginDescriptionAt(int index) const;

    // ── QML UI 能力查询（HomePanel / 页面路由使用）────────────────────────
    /** 插件是否提供自己的 QML 页面 */
    Q_INVOKABLE bool    pluginHasQmlUI(int index) const;
    /** 插件 QML 入口文件 URL（仅 hasQmlUI 为 true 时有效） */
    Q_INVOKABLE QUrl    pluginQmlUrl(int index) const;
    /** 插件在卡片上显示的图标 */
    Q_INVOKABLE QString pluginCardIcon(int index) const;
    /** 插件在卡片上显示的图片图标 URL */
    Q_INVOKABLE QUrl    pluginCardIconUrl(int index) const;
    /** 插件在卡片上显示的名称 */
    Q_INVOKABLE QString pluginCardName(int index) const;

signals:
    void pluginsChanged();
    void pluginLoadFailed(const QString& path, const QString& reason);

private:
    explicit PluginManager(QObject* parent = nullptr) : QObject(parent) {}
    ~PluginManager() override { unloadAll(); }

    struct PluginEntry {
        std::unique_ptr<QPluginLoader> loader;
        IAppPlugin*                    plugin = nullptr;
        PluginEntry() = default;
        PluginEntry(PluginEntry&&) = default;
        PluginEntry& operator=(PluginEntry&&) = default;
        PluginEntry(const PluginEntry&) = delete;
        PluginEntry& operator=(const PluginEntry&) = delete;
    };

    std::vector<PluginEntry> m_plugins;
};
