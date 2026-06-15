#include "PluginTemplateGenerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

static void require(bool condition, const QString& message)
{
    if (condition)
        return;

    QTextStream(stderr) << message << Qt::endl;
    std::exit(1);
}

static QString readFile(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly | QIODevice::Text), QStringLiteral("failed to read %1").arg(path));
    return QString::fromUtf8(file.readAll());
}

static QString createIconFile(const QString& parentDir)
{
    const QString iconPath = parentDir + QStringLiteral("/source-icon.png");
    QFile icon(iconPath);
    require(icon.open(QIODevice::WriteOnly), QStringLiteral("failed to create source icon"));
    icon.write("fake image bytes");
    icon.close();
    return iconPath;
}

static void verifyQmlPlugin(PluginTemplateGenerator& generator, const QString& parentDir)
{
    const QString sourceIconPath = createIconFile(parentDir);

    QVariantMap options;
    options.insert(QStringLiteral("pluginName"), QStringLiteral("CameraToolPlugin"));
    options.insert(QStringLiteral("displayName"), QStringLiteral("Camera Tool"));
    options.insert(QStringLiteral("description"), QStringLiteral("Camera utilities"));
    options.insert(QStringLiteral("icon"), QStringLiteral("C"));
    options.insert(QStringLiteral("iconPath"), sourceIconPath);
    options.insert(QStringLiteral("outputDir"), parentDir);
    options.insert(QStringLiteral("withQml"), true);

    const QVariantMap result = generator.generate(options);
    require(result.value(QStringLiteral("ok")).toBool(), result.value(QStringLiteral("message")).toString());

    const QString pluginDir = parentDir + QStringLiteral("/CameraToolPlugin");
    require(QFile::exists(pluginDir + QStringLiteral("/CMakeLists.txt")), QStringLiteral("missing CMakeLists.txt"));
    require(QFile::exists(pluginDir + QStringLiteral("/CameraToolPlugin.h")), QStringLiteral("missing header"));
    require(QFile::exists(pluginDir + QStringLiteral("/CameraToolPlugin.cpp")), QStringLiteral("missing source"));
    require(QFile::exists(pluginDir + QStringLiteral("/CameraToolPlugin.json")), QStringLiteral("missing metadata"));
    require(QFile::exists(pluginDir + QStringLiteral("/qml/CameraToolPluginView.qml")), QStringLiteral("missing qml view"));
    require(QFile::exists(pluginDir + QStringLiteral("/translations/CameraToolPlugin_zh_CN.ts")),
            QStringLiteral("missing plugin translation TS"));
    require(QFile::exists(pluginDir + QStringLiteral("/assets/icon.png")), QStringLiteral("missing copied icon asset"));

    const QString header = readFile(pluginDir + QStringLiteral("/CameraToolPlugin.h"));
    const QString source = readFile(pluginDir + QStringLiteral("/CameraToolPlugin.cpp"));
    const QString cmake = readFile(pluginDir + QStringLiteral("/CMakeLists.txt"));
    const QString qml = readFile(pluginDir + QStringLiteral("/qml/CameraToolPluginView.qml"));
    const QString translations = readFile(pluginDir + QStringLiteral("/translations/CameraToolPlugin_zh_CN.ts"));

    require(header.contains(QStringLiteral("class CameraToolPlugin : public QObject, public IAppPlugin")),
            QStringLiteral("header should implement IAppPlugin"));
    require(header.contains(QStringLiteral("QString id()          const override { return QStringLiteral(\"camera-tool-plugin\"); }")),
            QStringLiteral("plugin id should be kebab-case"));
    require(header.contains(QStringLiteral("bool initialize() override;")),
            QStringLiteral("initialize should be no-argument"));
    require(header.contains(QStringLiteral("bool hasQmlUI()        const override { return true; }")),
            QStringLiteral("QML plugin should expose UI"));
    require(header.contains(QStringLiteral("QUrl cardIconUrl() const override;")),
            QStringLiteral("plugin should expose an image icon URL"));
    require(header.contains(QStringLiteral("QString description() const override { return tr(\"Camera utilities\"); }")),
            QStringLiteral("description should be translatable"));
    require(header.contains(QStringLiteral("QString cardName()    const override { return tr(\"Camera Tool\"); }")),
            QStringLiteral("card name should be translatable"));
    require(header.contains(QStringLiteral("QStringList translationResourcePaths(const QString& languageName) const override;")),
            QStringLiteral("plugin should expose translation resource paths"));
    require(source.contains(QStringLiteral("qrc:/CameraToolPlugin/qml/CameraToolPluginView.qml")),
            QStringLiteral("source should return QML resource URL"));
    require(source.contains(QStringLiteral("qrc:/CameraToolPlugin/assets/icon.png")),
            QStringLiteral("source should return icon resource URL"));
    require(source.contains(QStringLiteral(":/CameraToolPlugin/i18n/CameraToolPlugin_zh_CN.qm")),
            QStringLiteral("source should return plugin translation resource URL"));
    require(cmake.contains(QStringLiteral("qt_add_qml_module(CameraToolPlugin")),
            QStringLiteral("QML plugin should add a QML module"));
    require(cmake.contains(QStringLiteral("NO_PLUGIN")),
            QStringLiteral("QML plugin should use NO_PLUGIN"));
    require(cmake.contains(QStringLiteral("qt_add_translations(CameraToolPlugin")) &&
            cmake.contains(QStringLiteral("translations/CameraToolPlugin_zh_CN.ts")) &&
            cmake.contains(QStringLiteral("RESOURCE_PREFIX \"/CameraToolPlugin/i18n\"")),
            QStringLiteral("CMake should embed plugin-owned translations"));
    require(cmake.contains(QStringLiteral("qt_add_resources(CameraToolPlugin")) &&
            cmake.contains(QStringLiteral("assets/icon.png")),
            QStringLiteral("CMake should embed copied icon assets"));
    require(qml.contains(QStringLiteral("property string pageTitle: qsTr(\"Camera Tool\")")),
            QStringLiteral("QML should translate pageTitle"));
    require(qml.contains(QStringLiteral("text: qsTr(\"Camera utilities\")")),
            QStringLiteral("QML should translate description text"));
    require(translations.contains(QStringLiteral("<name>CameraToolPlugin</name>")) &&
            translations.contains(QStringLiteral("<source>Camera Tool</source>")) &&
            translations.contains(QStringLiteral("<source>Camera utilities</source>")),
            QStringLiteral("translation TS should contain generated plugin text"));
}

static void verifyNoQmlPlugin(PluginTemplateGenerator& generator, const QString& parentDir)
{
    QVariantMap options;
    options.insert(QStringLiteral("pluginName"), QStringLiteral("AuditPlugin"));
    options.insert(QStringLiteral("outputDir"), parentDir);
    options.insert(QStringLiteral("withQml"), false);

    const QVariantMap result = generator.generate(options);
    require(result.value(QStringLiteral("ok")).toBool(), result.value(QStringLiteral("message")).toString());

    const QString pluginDir = parentDir + QStringLiteral("/AuditPlugin");
    require(!QFile::exists(pluginDir + QStringLiteral("/qml")), QStringLiteral("No-QML plugin should not create qml directory"));

    const QString header = readFile(pluginDir + QStringLiteral("/AuditPlugin.h"));
    const QString source = readFile(pluginDir + QStringLiteral("/AuditPlugin.cpp"));
    const QString cmake = readFile(pluginDir + QStringLiteral("/CMakeLists.txt"));

    require(!header.contains(QStringLiteral("hasQmlUI")), QStringLiteral("No-QML plugin should use default hasQmlUI"));
    require(!header.contains(QStringLiteral("qmlComponentUrl")), QStringLiteral("No-QML plugin should not declare QML URL"));
    require(header.contains(QStringLiteral("QStringList translationResourcePaths(const QString& languageName) const override;")),
            QStringLiteral("No-QML plugin should still expose translation resources"));
    require(source.contains(QStringLiteral(":/AuditPlugin/i18n/AuditPlugin_zh_CN.qm")),
            QStringLiteral("No-QML plugin should still return translation resource URL"));
    require(!source.contains(QStringLiteral("qrc:/")), QStringLiteral("No-QML plugin should not return qrc URL"));
    require(!cmake.contains(QStringLiteral("qt_add_qml_module")), QStringLiteral("No-QML plugin should not add QML module"));
    require(cmake.contains(QStringLiteral("qt_add_translations(AuditPlugin")),
            QStringLiteral("No-QML plugin should still embed translations"));
    require(QFile::exists(pluginDir + QStringLiteral("/translations/AuditPlugin_zh_CN.ts")),
            QStringLiteral("No-QML plugin should still create translation TS"));
}

static void verifyInvalidName(PluginTemplateGenerator& generator, const QString& parentDir)
{
    QVariantMap options;
    options.insert(QStringLiteral("pluginName"), QStringLiteral("123 Bad"));
    options.insert(QStringLiteral("outputDir"), parentDir);

    const QVariantMap result = generator.generate(options);
    require(!result.value(QStringLiteral("ok")).toBool(), QStringLiteral("invalid plugin name should fail"));
    require(result.value(QStringLiteral("message")).toString().contains(QStringLiteral("Invalid plugin name")),
            QStringLiteral("invalid name should return clear error"));
}

static void verifyExistingDirectory(PluginTemplateGenerator& generator, const QString& parentDir)
{
    QDir().mkpath(parentDir + QStringLiteral("/ExistingPlugin"));
    QFile marker(parentDir + QStringLiteral("/ExistingPlugin/marker.txt"));
    require(marker.open(QIODevice::WriteOnly | QIODevice::Text), QStringLiteral("failed to create marker"));
    marker.write("keep");
    marker.close();

    QVariantMap options;
    options.insert(QStringLiteral("pluginName"), QStringLiteral("ExistingPlugin"));
    options.insert(QStringLiteral("outputDir"), parentDir);

    const QVariantMap result = generator.generate(options);
    require(!result.value(QStringLiteral("ok")).toBool(), QStringLiteral("existing directory should fail"));
    require(readFile(parentDir + QStringLiteral("/ExistingPlugin/marker.txt")) == QStringLiteral("keep"),
            QStringLiteral("existing directory should not be overwritten"));
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tempDir;
    require(tempDir.isValid(), QStringLiteral("failed to create temporary directory"));

    PluginTemplateGenerator generator;
    verifyQmlPlugin(generator, tempDir.path());
    verifyNoQmlPlugin(generator, tempDir.path());
    verifyInvalidName(generator, tempDir.path());
    verifyExistingDirectory(generator, tempDir.path());
    return 0;
}
