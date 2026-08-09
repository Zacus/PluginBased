#include "BuildInfo.h"

#include "BuildInfoData.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace PluginBased::App {

namespace {

QString fromBuildData(const char* value)
{
    return QString::fromUtf8(value);
}

QJsonObject buildInfoObject()
{
    return {
        {QStringLiteral("schemaVersion"), BuildInfoData::SchemaVersion},
        {QStringLiteral("productName"), BuildInfo::productName()},
        {QStringLiteral("productVersion"), BuildInfo::productVersion()},
        {QStringLiteral("displayVersion"), BuildInfo::displayVersion()},
        {QStringLiteral("gitCommit"), BuildInfo::gitCommit()},
        {QStringLiteral("gitShortCommit"), BuildInfo::buildNumber()},
        {QStringLiteral("gitTag"), BuildInfo::gitTag()},
        {QStringLiteral("gitTreeState"), BuildInfo::treeState()},
        {QStringLiteral("buildType"), BuildInfo::buildType()},
        {QStringLiteral("platform"), BuildInfo::platform()},
        {QStringLiteral("architecture"), BuildInfo::architecture()},
        {QStringLiteral("compiler"), BuildInfo::compiler()},
        {QStringLiteral("qtVersion"), BuildInfo::qtVersion()},
    };
}

} // namespace

QString BuildInfo::productName()
{
    return fromBuildData(BuildInfoData::ProductName);
}

QString BuildInfo::productVersion()
{
    return fromBuildData(BuildInfoData::ProductVersion);
}

QString BuildInfo::displayVersion()
{
    return fromBuildData(BuildInfoData::DisplayVersion);
}

QString BuildInfo::buildNumber()
{
    return fromBuildData(BuildInfoData::GitShortCommit);
}

QString BuildInfo::gitCommit()
{
    return fromBuildData(BuildInfoData::GitCommit);
}

QString BuildInfo::gitTag()
{
    return fromBuildData(BuildInfoData::GitTag);
}

QString BuildInfo::treeState()
{
    return fromBuildData(BuildInfoData::GitTreeState);
}

QString BuildInfo::buildType()
{
    return fromBuildData(BuildInfoData::BuildType);
}

QString BuildInfo::platform()
{
    return fromBuildData(BuildInfoData::Platform);
}

QString BuildInfo::architecture()
{
    return fromBuildData(BuildInfoData::Architecture);
}

QString BuildInfo::compiler()
{
    return fromBuildData(BuildInfoData::Compiler);
}

QString BuildInfo::qtVersion()
{
    return fromBuildData(BuildInfoData::QtVersion);
}

QString BuildInfo::conciseVersion()
{
    return QStringLiteral("%1 %2 (build %3)")
        .arg(productName(), productVersion(), buildNumber());
}

QString BuildInfo::diagnosticSummary()
{
    return QStringLiteral(
               "%1\n"
               "Display version: %2\n"
               "Commit: %3\n"
               "Source state: %4\n"
               "Build: %5\n"
               "Platform: %6 %7\n"
               "Compiler: %8\n"
               "Qt: %9")
        .arg(conciseVersion(),
             displayVersion(),
             gitCommit().isEmpty() ? QStringLiteral("unknown") : gitCommit(),
             treeState(),
             buildType(),
             platform(),
             architecture(),
             compiler(),
             qtVersion());
}

QByteArray BuildInfo::json()
{
    return QJsonDocument(buildInfoObject()).toJson(QJsonDocument::Indented);
}

} // namespace PluginBased::App
