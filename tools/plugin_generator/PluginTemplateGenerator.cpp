#include "PluginTemplateGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString sourceRoot()
{
#ifdef PLUGINBASED_SOURCE_DIR
    return QStringLiteral(PLUGINBASED_SOURCE_DIR);
#else
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
#endif
}

bool writeTextFile(const QString& path, const QString& text, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *error = QStringLiteral("Failed to write %1: %2").arg(path, file.errorString());
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    return true;
}

} // namespace

PluginTemplateGenerator::PluginTemplateGenerator(QObject* parent)
    : QObject(parent)
{}

QString PluginTemplateGenerator::defaultOutputDir() const
{
    return QDir::cleanPath(sourceRoot() + QStringLiteral("/plugins"));
}

QVariantMap PluginTemplateGenerator::generate(const QVariantMap& input)
{
    Options options;
    const QVariantMap parsed = parseOptions(input, &options);
    if (!parsed.value(QStringLiteral("ok")).toBool())
        return parsed;

    return writePlugin(options);
}

QVariantMap PluginTemplateGenerator::parseOptions(const QVariantMap& input, Options* options) const
{
    options->pluginName = input.value(QStringLiteral("pluginName")).toString().trimmed();
    options->displayName = input.value(QStringLiteral("displayName")).toString().trimmed();
    options->description = input.value(QStringLiteral("description")).toString().trimmed();
    options->icon = input.value(QStringLiteral("icon")).toString().trimmed();
    options->iconPath = input.value(QStringLiteral("iconPath")).toString().trimmed();
    options->outputDir = input.value(QStringLiteral("outputDir")).toString().trimmed();
    options->withQml = input.value(QStringLiteral("withQml"), true).toBool();

    if (options->pluginName.isEmpty())
        return failure(QStringLiteral("插件名不能为空"));

    if (!isValidPluginName(options->pluginName)) {
        return failure(QStringLiteral("Invalid plugin name: %1. Use letters, numbers, and underscores; start with a letter.")
                           .arg(options->pluginName));
    }

    if (options->displayName.isEmpty())
        options->displayName = options->pluginName;
    if (options->description.isEmpty())
        options->description = QStringLiteral("Generated PluginBased plugin");
    if (options->icon.isEmpty())
        options->icon = QStringLiteral("⬡");
    if (options->outputDir.isEmpty())
        options->outputDir = defaultOutputDir();

    if (!options->iconPath.isEmpty()) {
        QFileInfo iconInfo(options->iconPath);
        if (!iconInfo.exists() || !iconInfo.isFile())
            return failure(QStringLiteral("Icon file does not exist: %1").arg(options->iconPath));

        QString suffix = iconInfo.suffix().toLower();
        if (suffix.isEmpty())
            suffix = QStringLiteral("png");
        options->iconAssetName = QStringLiteral("icon.%1").arg(suffix);
    }

    options->pluginId = toPluginId(options->pluginName);
    return success(QString());
}

QVariantMap PluginTemplateGenerator::writePlugin(const Options& options) const
{
    QDir outputParent(options.outputDir);
    if (!outputParent.exists() && !outputParent.mkpath(QStringLiteral("."))) {
        return failure(QStringLiteral("Failed to create output directory: %1")
                           .arg(QDir::cleanPath(options.outputDir)));
    }

    const QString pluginPath = outputParent.absoluteFilePath(options.pluginName);
    if (QFileInfo::exists(pluginPath))
        return failure(QStringLiteral("Plugin directory already exists: %1").arg(pluginPath));

    QDir pluginDir;
    if (!pluginDir.mkpath(pluginPath))
        return failure(QStringLiteral("Failed to create plugin directory: %1").arg(pluginPath));

    if (options.withQml && !pluginDir.mkpath(pluginPath + QStringLiteral("/qml")))
        return failure(QStringLiteral("Failed to create qml directory: %1/qml").arg(pluginPath));
    if (!options.iconPath.isEmpty() && !pluginDir.mkpath(pluginPath + QStringLiteral("/assets")))
        return failure(QStringLiteral("Failed to create assets directory: %1/assets").arg(pluginPath));

    QString error;
    const struct FileSpec {
        QString relativePath;
        QString text;
    } files[] = {
        {QStringLiteral("CMakeLists.txt"), cmakeText(options)},
        {options.pluginName + QStringLiteral(".h"), headerText(options)},
        {options.pluginName + QStringLiteral(".cpp"), sourceText(options)},
        {options.pluginName + QStringLiteral(".json"), metadataText(options)},
    };

    for (const FileSpec& file : files) {
        if (!writeTextFile(pluginPath + QStringLiteral("/") + file.relativePath, file.text, &error))
            return failure(error);
    }

    if (!copyIconAsset(options, pluginPath, &error))
        return failure(error);

    if (options.withQml) {
        const QString viewName = QStringLiteral("qml/%1View.qml").arg(options.pluginName);
        if (!writeTextFile(pluginPath + QStringLiteral("/") + viewName, qmlText(options), &error))
            return failure(error);
    }

    return success(QDir::cleanPath(pluginPath));
}

bool PluginTemplateGenerator::copyIconAsset(const Options& options, const QString& pluginPath, QString* error) const
{
    if (options.iconPath.isEmpty())
        return true;

    const QString targetPath = pluginPath + QStringLiteral("/assets/") + options.iconAssetName;
    if (!QFile::copy(options.iconPath, targetPath)) {
        *error = QStringLiteral("Failed to copy icon to %1").arg(targetPath);
        return false;
    }
    return true;
}

bool PluginTemplateGenerator::isValidPluginName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
    return pattern.match(name).hasMatch();
}

QString PluginTemplateGenerator::toPluginId(const QString& name)
{
    QString result;
    result.reserve(name.size() + 4);

    for (int i = 0; i < name.size(); ++i) {
        const QChar ch = name.at(i);
        if (ch == QLatin1Char('_')) {
            if (!result.endsWith(QLatin1Char('-')))
                result += QLatin1Char('-');
            continue;
        }

        if (ch.isUpper() && i > 0) {
            const QChar prev = name.at(i - 1);
            if ((prev.isLower() || prev.isDigit()) && !result.endsWith(QLatin1Char('-')))
                result += QLatin1Char('-');
        }
        result += ch.toLower();
    }

    while (result.contains(QStringLiteral("--")))
        result.replace(QStringLiteral("--"), QStringLiteral("-"));
    if (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result;
}

QString PluginTemplateGenerator::cppString(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    escaped.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return escaped;
}

QString PluginTemplateGenerator::jsonString(const QString& value)
{
    return cppString(value);
}

QString PluginTemplateGenerator::qmlString(const QString& value)
{
    return cppString(value);
}

QVariantMap PluginTemplateGenerator::success(const QString& path)
{
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), path},
        {QStringLiteral("message"), QStringLiteral("插件已生成")},
    };
}

QVariantMap PluginTemplateGenerator::failure(const QString& message)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), message},
    };
}

QString PluginTemplateGenerator::headerText(const Options& options) const
{
    const QString qObject = QStringLiteral("Q_") + QStringLiteral("OBJECT");
    const QString qInterfaces = QStringLiteral("Q_") + QStringLiteral("INTERFACES(IAppPlugin)");
    const QString qMetadata = QStringLiteral("Q_")
        + QStringLiteral("PLUGIN_METADATA(IID IAppPlugin_IID FILE \"%1.json\")").arg(options.pluginName);

    QString qmlDeclarations;
    if (options.withQml) {
        qmlDeclarations = QStringLiteral(R"(
    bool hasQmlUI()        const override { return true; }
    QUrl qmlComponentUrl() const override;
)");
    }
    QString iconUrlDeclaration;
    if (!options.iconAssetName.isEmpty()) {
        iconUrlDeclaration = QStringLiteral(R"(
    QUrl cardIconUrl() const override;
)");
    }

    return QStringLiteral(R"(#pragma once

#include <QObject>
#include "IAppPlugin.h"

class %1 : public QObject, public IAppPlugin
{
    %7
    %8
    %9

public:
    explicit %1(QObject* parent = nullptr);
    ~%1() override;

    QString id()          const override { return QStringLiteral("%2"); }
    QString name()        const override { return QStringLiteral("%1"); }
    QString version()     const override { return QStringLiteral("1.0.0"); }
    QString description() const override { return QStringLiteral("%3"); }
    QString cardIcon()    const override { return QStringLiteral("%4"); }
    QString cardName()    const override { return QStringLiteral("%5"); }

    bool initialize() override;
    void shutdown() override;
%6};
)")
        .arg(options.pluginName,
             cppString(options.pluginId),
             cppString(options.description),
             cppString(options.icon),
             cppString(options.displayName),
             qmlDeclarations + iconUrlDeclaration,
             qObject,
             qInterfaces,
             qMetadata);
}

QString PluginTemplateGenerator::sourceText(const Options& options) const
{
    QString qmlImplementation;
    if (options.withQml) {
        qmlImplementation = QStringLiteral(R"(
QUrl %1::qmlComponentUrl() const
{
    return QUrl(QStringLiteral("qrc:/%1/qml/%1View.qml"));
}
)").arg(options.pluginName);
    }

    QString iconImplementation;
    if (!options.iconAssetName.isEmpty()) {
        iconImplementation = QStringLiteral(R"(
QUrl %1::cardIconUrl() const
{
    return QUrl(QStringLiteral("qrc:/%1/assets/%2"));
}
)").arg(options.pluginName, options.iconAssetName);
    }

    return QStringLiteral(R"CPP(#include "%1.h"
#include "Logger.h"

%1::%1(QObject* parent)
    : QObject(parent)
{}

%1::~%1()
{
    shutdown();
}

bool %1::initialize()
{
    LOG_INFO("%1::initialize()");
    return true;
}

void %1::shutdown()
{
    LOG_INFO("%1::shutdown()");
}
%2%3)CPP")
        .arg(options.pluginName, qmlImplementation, iconImplementation);
}

QString PluginTemplateGenerator::metadataText(const Options& options) const
{
    return QStringLiteral(R"({
    "IID": "com.pluginbased.IAppPlugin/1.0",
    "MetaData": {
        "name": "%1",
        "version": "1.0.0"
    }
}
)")
        .arg(jsonString(options.pluginName));
}

QString PluginTemplateGenerator::cmakeText(const Options& options) const
{
    QString qmlBlock;
    QString quickLink;
    if (options.withQml) {
        qmlBlock = QStringLiteral(R"(
qt_add_qml_module(%1
    NO_PLUGIN
    URI     %1
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/%1"
    QML_FILES
        qml/%1View.qml
)
)").arg(options.pluginName);
        quickLink = QStringLiteral(R"(
    Qt6::Quick
    QtQuickComponents)");
    }
    QString resourceBlock;
    if (!options.iconAssetName.isEmpty()) {
        resourceBlock = QStringLiteral(R"(
qt_add_resources(%1 "%1Assets"
    PREFIX "/%1"
    FILES
        assets/%2
)
)").arg(options.pluginName, options.iconAssetName);
    }

    return QStringLiteral(R"(add_library(%1 MODULE
    %1.h
    %1.cpp
)
%2
%4
set_target_properties(%1 PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
)

target_include_directories(%1 PRIVATE
    ${CMAKE_SOURCE_DIR}/plugin
    ${CMAKE_SOURCE_DIR}/logger
)

target_link_libraries(%1 PRIVATE
    Qt6::Core
    PluginBasedPlugin
    PluginBasedLogger%3
)

if(APPLE)
    install(TARGETS %1
        LIBRARY DESTINATION "$<TARGET_BUNDLE_DIR:PluginBasedApp>/Contents/PlugIns"
    )
else()
    install(TARGETS %1
        LIBRARY DESTINATION plugins
        RUNTIME DESTINATION plugins
    )
endif()
)")
        .arg(options.pluginName, qmlBlock, quickLink, resourceBlock);
}

QString PluginTemplateGenerator::qmlText(const Options& options) const
{
    return QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    property string pageTitle: "%1"

    Rectangle {
        anchors.fill: parent
        color: "#0f0f13"
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: root.pageTitle
            color: "#e8e8f0"
            font.pixelSize: 24
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: "%2"
            color: "#808096"
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
)")
        .arg(qmlString(options.displayName), qmlString(options.description));
}
