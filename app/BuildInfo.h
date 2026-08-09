#pragma once

#include <QByteArray>
#include <QString>

namespace PluginBased::App {

class BuildInfo final
{
public:
    BuildInfo() = delete;

    static QString productName();
    static QString productVersion();
    static QString displayVersion();
    static QString buildNumber();
    static QString gitCommit();
    static QString gitTag();
    static QString treeState();
    static QString buildType();
    static QString platform();
    static QString architecture();
    static QString compiler();
    static QString qtVersion();
    static QString conciseVersion();
    static QString diagnosticSummary();
    static QByteArray json();
};

} // namespace PluginBased::App
