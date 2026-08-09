#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "BuildInfo.h"

using PluginBased::App::BuildInfo;

class BuildInfoTest : public QObject
{
    Q_OBJECT

private slots:
    void productVersionIsStable();
    void diagnosticJsonMatchesGetters();
    void diagnosticDataDoesNotExposeMachineOrBranchIdentity();
};

void BuildInfoTest::productVersionIsStable()
{
    QCOMPARE(BuildInfo::productName(), QStringLiteral("PluginBased"));
    QCOMPARE(BuildInfo::productVersion(), QStringLiteral(PLUGINBASED_TEST_VERSION));
    QCOMPARE(BuildInfo::buildNumber().size(), 8);
    QVERIFY(BuildInfo::conciseVersion().startsWith(
        QStringLiteral("PluginBased ") + BuildInfo::productVersion()));
}

void BuildInfoTest::diagnosticJsonMatchesGetters()
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(BuildInfo::json(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonObject object = document.object();
    QCOMPARE(object.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(object.value(QStringLiteral("productVersion")).toString(),
             BuildInfo::productVersion());
    QCOMPARE(object.value(QStringLiteral("displayVersion")).toString(),
             BuildInfo::displayVersion());
    QCOMPARE(object.value(QStringLiteral("gitCommit")).toString(),
             BuildInfo::gitCommit());
    QCOMPARE(object.value(QStringLiteral("gitShortCommit")).toString(),
             BuildInfo::buildNumber());
    QCOMPARE(object.value(QStringLiteral("gitTreeState")).toString(),
             BuildInfo::treeState());
}

void BuildInfoTest::diagnosticDataDoesNotExposeMachineOrBranchIdentity()
{
    const QJsonObject object = QJsonDocument::fromJson(BuildInfo::json()).object();
    for (const QString& forbidden : {
             QStringLiteral("gitRef"),
             QStringLiteral("branch"),
             QStringLiteral("hostname"),
             QStringLiteral("sourceDir"),
             QStringLiteral("remoteUrl"),
         }) {
        QVERIFY2(!object.contains(forbidden), qPrintable(forbidden));
        QVERIFY2(!BuildInfo::diagnosticSummary().contains(forbidden),
                 qPrintable(forbidden));
    }
}

QTEST_APPLESS_MAIN(BuildInfoTest)

#include "tst_build_info.moc"
