#pragma once

// Resolves the runtime plugin directory for AppController.
// This helper keeps platform/build-layout path probing out of the QML-facing controller.

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
