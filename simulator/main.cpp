#include <QCommandLineParser>
#include <QCoreApplication>

#include "SensorSimulator.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sb-simulator"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostOpt({"b", "broker"}, "MQTT broker host", "host", "localhost");
    const QCommandLineOption portOpt({"p", "port"}, "MQTT broker port", "port", "1883");
    parser.addOptions({hostOpt, portOpt});
    parser.process(app);

    SensorSimulator simulator(parser.value(hostOpt), parser.value(portOpt).toUShort());
    simulator.start();
    return app.exec();
}
