#pragma once

// 解析 AppController 启动时使用的插件目录。
// 平台差异和开发/打包目录探测集中在这里，避免 QML 门面类承担路径细节。

#include <QString>

namespace PluginBased::App {

struct PluginPathResolution
{
    QString pluginDir;
    int candidateCount = 0;
};

class PluginPathResolver
{
public:
    static PluginPathResolution resolve(const QString& appDir);
};

} // namespace PluginBased::App
