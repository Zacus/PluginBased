#include "PluginPathResolver.h"

// 实现宿主插件目录发现，覆盖开发构建和发布包两种布局。
// 候选路径顺序沿用原 AppController 启动逻辑，避免改变插件查找行为。

#include "Logger.h"

#include <QDir>
#include <QStringList>

namespace PluginBased::App {

PluginPathResolution PluginPathResolver::resolve(const QString& appDir)
{
    const QStringList candidates = {
        appDir + "/plugins",
        appDir + "/../PlugIns",
        appDir + "/../plugins",
        appDir + "/../../../../plugins",
    };

#if defined(Q_OS_WIN)
    const QStringList filters { "*.dll" };
#else
    const QStringList filters { "*.so" };
#endif

    PluginPathResolution result;
    result.candidateCount = candidates.size();

    for (const QString& candidate : candidates)
    {
        const QString clean = QDir::cleanPath(candidate);
        QDir dir(clean);
        if (dir.exists() && !dir.entryList(filters, QDir::Files).isEmpty())
        {
            result.pluginDir = clean;
            return result;
        }
        if (dir.exists())
        {
            LOG_DEBUG("AppController: candidate exists but empty, skipping: {}",
                      clean.toStdString());
        }
    }

    return result;
}

} // namespace PluginBased::App
