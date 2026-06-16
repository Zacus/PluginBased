#include "PluginPathResolver.h"

// Implements host plugin directory discovery for development and packaged layouts.
// Candidate order intentionally mirrors the previous AppController startup logic.

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
