#include "PluginTemplateRenderer.h"

// Implements text templates used by PluginTemplateGenerator.
// These methods are pure string rendering and intentionally avoid filesystem access.

QString PluginTemplateRenderer::cppString(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    escaped.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    escaped.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
    return escaped;
}

QString PluginTemplateRenderer::jsonString(const QString& value)
{
    return cppString(value);
}

QString PluginTemplateRenderer::qmlString(const QString& value)
{
    return cppString(value);
}

QString PluginTemplateRenderer::xmlString(const QString& value)
{
    return value.toHtmlEscaped();
}

QString PluginTemplateRenderer::headerText(const PluginGeneratorOptions& options) const
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
    QString description() const override { return tr("%3"); }
    QString cardIcon()    const override { return QStringLiteral("%4"); }
    QString cardName()    const override { return tr("%5"); }

    bool initialize() override;
    void shutdown() override;
    QStringList translationResourcePaths(const QString& languageName) const override;
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

QString PluginTemplateRenderer::sourceText(const PluginGeneratorOptions& options) const
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
%2%3
QStringList %1::translationResourcePaths(const QString& languageName) const
{
    if (languageName == QStringLiteral("zh_CN"))
        return { QStringLiteral(":/%1/i18n/%1_zh_CN.qm") };

    return {};
}
)CPP")
        .arg(options.pluginName, qmlImplementation, iconImplementation);
}

QString PluginTemplateRenderer::metadataText(const PluginGeneratorOptions& options) const
{
    return QStringLiteral(R"({
    "IID": "com.pluginbased.IAppPlugin/1.0",
    "MetaData": {
        "schemaVersion": 1,
        "apiVersion": 1,
        "abiVersion": 2,
        "id": "%2",
        "name": "%1",
        "version": "1.0.0",
        "description": "%4",
        "hasQml": %3
    }
}
)")
        .arg(jsonString(options.pluginName),
             jsonString(options.pluginId),
             options.withQml ? QStringLiteral("true") : QStringLiteral("false"),
             jsonString(options.description));
}

QString PluginTemplateRenderer::cmakeText(const PluginGeneratorOptions& options) const
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
qt_add_translations(%1
    TS_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/translations/%1_zh_CN.ts"
    RESOURCE_PREFIX "/%1/i18n"
)

set_target_properties(%1 PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_BINARY_DIR}/plugins"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/%1.json"
    "${CMAKE_BINARY_DIR}/plugins/%1.json"
    COPYONLY
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
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/%1.json"
        DESTINATION "$<TARGET_BUNDLE_DIR:PluginBasedApp>/Contents/PlugIns"
    )
else()
    install(TARGETS %1
        LIBRARY DESTINATION plugins
        RUNTIME DESTINATION plugins
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/%1.json"
        DESTINATION plugins
    )
endif()
)")
        .arg(options.pluginName, qmlBlock, quickLink, resourceBlock);
}

QString PluginTemplateRenderer::qmlText(const PluginGeneratorOptions& options) const
{
    return QStringLiteral(R"(import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QuickUI.Components 1.0

Item {
    id: root

    property string pageTitle: qsTr("%1")

    Rectangle {
        anchors.fill: parent
        color: ComponentTheme.surface
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 520)
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: root.pageTitle
            color: ComponentTheme.textPrimary
            font.pixelSize: 24
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("%2")
            color: ComponentTheme.textSecondary
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
)")
        .arg(qmlString(options.displayName), qmlString(options.description));
}

QString PluginTemplateRenderer::translationText(const PluginGeneratorOptions& options) const
{
    QString qmlContext;
    if (options.withQml) {
        qmlContext = QStringLiteral(R"(
<context>
    <name>%1View</name>
    <message>
        <source>%2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>%3</source>
        <translation type="unfinished"></translation>
    </message>
</context>
)")
            .arg(xmlString(options.pluginName),
                 xmlString(options.displayName),
                 xmlString(options.description));
    }

    return QStringLiteral(R"(<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>%1</name>
    <message>
        <source>%2</source>
        <translation type="unfinished"></translation>
    </message>
    <message>
        <source>%3</source>
        <translation type="unfinished"></translation>
    </message>
</context>%4</TS>
)")
        .arg(xmlString(options.pluginName),
             xmlString(options.displayName),
             xmlString(options.description),
             qmlContext);
}
