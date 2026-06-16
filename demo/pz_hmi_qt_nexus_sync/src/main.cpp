#include "backend_facade.h"
#include "demo_data_provider.h"
#include "telemetry_model.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Nexus Sync HMI");
    QGuiApplication::setOrganizationName("Nexus");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({ "demo", "Run with local simulated data." });
    parser.addOption({ "fullscreen", "Start fullscreen." });
    parser.process(app);

    TelemetryModel model;
    DemoDataProvider demoProvider;
    BackendFacade backend(&model, &demoProvider);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("telemetry", &model);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("startFullscreen", parser.isSet("fullscreen"));
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    demoProvider.start();
    return app.exec();
}
