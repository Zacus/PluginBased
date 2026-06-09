#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "PluginDiscovery.h"

namespace PluginDiscovery = PluginBased::Plugins::PluginDiscovery;

class PluginDiscoveryTest : public QObject
{
    Q_OBJECT

private slots:
    void stripsPlatformLibraryPrefix();
    void resolvesMetadataPathBesideLibrary();
    void resolvesManifestBesidePluginDirectory();
    void readsManifestPluginNames();
    void rejectsUnsafeManifestPluginName();
    void findsPluginLibraryFile();

private:
    static QString writeFile(const QString& directory, const QString& name, const QByteArray& content)
    {
        const QString path = QDir(directory).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        file.write(content);
        return path;
    }
};

void PluginDiscoveryTest::stripsPlatformLibraryPrefix()
{
    QCOMPARE(PluginDiscovery::pluginNameFromLibraryFile(QStringLiteral("libPlayPlugin.so")),
             QStringLiteral("PlayPlugin"));
    QCOMPARE(PluginDiscovery::pluginNameFromLibraryFile(QStringLiteral("DummyPlugin.dll")),
             QStringLiteral("DummyPlugin"));
}

void PluginDiscoveryTest::resolvesMetadataPathBesideLibrary()
{
    const QString path = QDir::cleanPath(QStringLiteral("/tmp/pluginbased/plugins/libPlayPlugin.so"));
    QCOMPARE(QDir::cleanPath(PluginDiscovery::metadataJsonPathForLibrary(path)),
             QStringLiteral("/tmp/pluginbased/plugins/PlayPlugin.json"));
}

void PluginDiscoveryTest::resolvesManifestBesidePluginDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir root(dir.path());
    QVERIFY(root.mkpath(QStringLiteral("plugins")));

    const QDir pluginDir(root.filePath(QStringLiteral("plugins")));
    QCOMPARE(QDir::cleanPath(PluginDiscovery::manifestFilePath(pluginDir)),
             QDir::cleanPath(root.filePath(QStringLiteral("plugins.json"))));
}

void PluginDiscoveryTest::readsManifestPluginNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString manifestPath = writeFile(dir.path(),
                                           QStringLiteral("plugins.json"),
                                           R"json({"plugins":["DummyPlugin","PlayPlugin","DummyPlugin"]})json");
    QVERIFY(!manifestPath.isEmpty());

    QString error;
    const QStringList names = PluginDiscovery::manifestPluginNames(manifestPath, &error);

    QCOMPARE(error, QString());
    QCOMPARE(names, QStringList({QStringLiteral("DummyPlugin"), QStringLiteral("PlayPlugin")}));
}

void PluginDiscoveryTest::rejectsUnsafeManifestPluginName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString manifestPath = writeFile(dir.path(),
                                           QStringLiteral("plugins.json"),
                                           R"json({"plugins":["../BadPlugin"]})json");
    QVERIFY(!manifestPath.isEmpty());

    QString error;
    const QStringList names = PluginDiscovery::manifestPluginNames(manifestPath, &error);

    QVERIFY(names.isEmpty());
    QVERIFY(error.contains(QStringLiteral("invalid plugin name")));
}

void PluginDiscoveryTest::findsPluginLibraryFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir pluginDir(dir.path());

    const QStringList entries = {
        QStringLiteral("libDummyPlugin.so"),
        QStringLiteral("libPlayPlugin.so"),
    };

    QCOMPARE(PluginDiscovery::findPluginLibraryFile(pluginDir, QStringLiteral("PlayPlugin"), entries),
             pluginDir.absoluteFilePath(QStringLiteral("libPlayPlugin.so")));
    QCOMPARE(PluginDiscovery::findPluginLibraryFile(pluginDir, QStringLiteral("MissingPlugin"), entries),
             QString());
}

QTEST_MAIN(PluginDiscoveryTest)

#include "tst_plugin_discovery.moc"
