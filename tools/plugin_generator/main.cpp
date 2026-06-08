#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("PluginBased"));
    app.setApplicationName(QStringLiteral("PluginGenerator"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/PluginGenerator/qml/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject* object, const QUrl& objectUrl) {
            if (!object && url == objectUrl)
                QCoreApplication::exit(1);
        },
        Qt::QueuedConnection
    );

    engine.load(url);
    return app.exec();
}
