#include <QtTest/QtTest>

#include <QJsonDocument>

#include "PluginMetadataValidator.h"

using PluginBased::Plugins::PluginMetadataValidationResult;
using PluginBased::Plugins::PluginMetadataValidator;

class PluginMetadataValidatorTest : public QObject
{
    Q_OBJECT

private slots:
    void acceptsValidMetadata();
    void rejectsMissingMetaData();
    void rejectsMissingAbiVersion();
    void rejectsWrongAbiVersion();
    void rejectsWrongIid();
    void rejectsUnsafeId();
    void rejectsNameMismatch();
    void rejectsInvalidVersion();
    void acceptsRuntimeConsistency();
    void reportsRuntimeConsistencyMismatch();

private:
    static QJsonDocument documentFromJson(const QByteArray& json)
    {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(json, &error);
        Q_ASSERT(error.error == QJsonParseError::NoError);
        return document;
    }

    static QJsonDocument validDocument()
    {
        return documentFromJson(R"json({
  "IID": "com.pluginbased.IAppPlugin/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "abiVersion": 2,
    "id": "play-plugin",
    "name": "PlayPlugin",
    "version": "1.0.0",
    "description": "Player plugin",
    "hasQml": true
  }
})json");
    }
};

void PluginMetadataValidatorTest::acceptsValidMetadata()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(validDocument(),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.metadata.iid, QStringLiteral("com.pluginbased.IAppPlugin/1.0"));
    QCOMPARE(result.metadata.schemaVersion, 1);
    QCOMPARE(result.metadata.apiVersion, 1);
    QCOMPARE(result.metadata.abiVersion, 2);
    QCOMPARE(result.metadata.id, QStringLiteral("play-plugin"));
    QCOMPARE(result.metadata.name, QStringLiteral("PlayPlugin"));
    QCOMPARE(result.metadata.version, QStringLiteral("1.0.0"));
    QCOMPARE(result.metadata.description, QStringLiteral("Player plugin"));
    QCOMPARE(result.metadata.hasQml, true);
}

void PluginMetadataValidatorTest::rejectsMissingMetaData()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(documentFromJson(R"json({
  "IID": "com.pluginbased.IAppPlugin/1.0"
})json"),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("MetaData")));
}

void PluginMetadataValidatorTest::rejectsMissingAbiVersion()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(documentFromJson(R"json({
  "IID": "com.pluginbased.IAppPlugin/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "id": "play-plugin",
    "name": "PlayPlugin",
    "version": "1.0.0",
    "description": "Player plugin",
    "hasQml": true
  }
})json"),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("abiVersion")));
}

void PluginMetadataValidatorTest::rejectsWrongAbiVersion()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(documentFromJson(R"json({
  "IID": "com.pluginbased.IAppPlugin/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "abiVersion": 1,
    "id": "play-plugin",
    "name": "PlayPlugin",
    "version": "1.0.0",
    "description": "Player plugin",
    "hasQml": true
  }
})json"),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("abiVersion")));
    QVERIFY(result.error.contains(QStringLiteral("expected 2")));
}

void PluginMetadataValidatorTest::rejectsWrongIid()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(documentFromJson(R"json({
  "IID": "com.example.Other/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "abiVersion": 2,
    "id": "play-plugin",
    "name": "PlayPlugin",
    "version": "1.0.0",
    "description": "Player plugin",
    "hasQml": true
  }
})json"),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("IID")));
}

void PluginMetadataValidatorTest::rejectsUnsafeId()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(documentFromJson(R"json({
  "IID": "com.pluginbased.IAppPlugin/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "abiVersion": 2,
    "id": "../PlayPlugin",
    "name": "PlayPlugin",
    "version": "1.0.0",
    "description": "Player plugin",
    "hasQml": true
  }
})json"),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("id")));
}

void PluginMetadataValidatorTest::rejectsNameMismatch()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(validDocument(),
                                                  QStringLiteral("DummyPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("name")));
    QVERIFY(result.error.contains(QStringLiteral("DummyPlugin")));
}

void PluginMetadataValidatorTest::rejectsInvalidVersion()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(documentFromJson(R"json({
  "IID": "com.pluginbased.IAppPlugin/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "abiVersion": 2,
    "id": "play-plugin",
    "name": "PlayPlugin",
    "version": "1",
    "description": "Player plugin",
    "hasQml": true
  }
})json"),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));

    QVERIFY(!result.ok);
    QVERIFY(result.error.contains(QStringLiteral("version")));
}

void PluginMetadataValidatorTest::acceptsRuntimeConsistency()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(validDocument(),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));
    QVERIFY2(result.ok, qPrintable(result.error));

    const QString error = PluginMetadataValidator::runtimeConsistencyError(result.metadata,
                                                                          QStringLiteral("play-plugin"),
                                                                          QStringLiteral("PlayPlugin"),
                                                                          QStringLiteral("1.0.0"),
                                                                          true);

    QCOMPARE(error, QString());
}

void PluginMetadataValidatorTest::reportsRuntimeConsistencyMismatch()
{
    const PluginMetadataValidationResult result =
        PluginMetadataValidator::validateDocument(validDocument(),
                                                  QStringLiteral("PlayPlugin"),
                                                  QStringLiteral("PlayPlugin.json"));
    QVERIFY2(result.ok, qPrintable(result.error));

    const QString error = PluginMetadataValidator::runtimeConsistencyError(result.metadata,
                                                                          QStringLiteral("wrong-plugin"),
                                                                          QStringLiteral("PlayPlugin"),
                                                                          QStringLiteral("1.0.0"),
                                                                          true);

    QVERIFY(error.contains(QStringLiteral("id")));
    QVERIFY(error.contains(QStringLiteral("play-plugin")));
    QVERIFY(error.contains(QStringLiteral("wrong-plugin")));
}

QTEST_MAIN(PluginMetadataValidatorTest)

#include "tst_plugin_metadata_validator.moc"
