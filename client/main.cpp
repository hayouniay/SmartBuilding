#include <QApplication>
#include <QCommandLineParser>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Smart Building Monitor"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostOpt({"b", "broker"}, "MQTT broker host", "host", "localhost");
    const QCommandLineOption portOpt({"p", "port"}, "MQTT broker port", "port", "1883");
    parser.addOptions({hostOpt, portOpt});
    parser.process(app);

    MainWindow window(parser.value(hostOpt), parser.value(portOpt).toUShort());
    window.show();
    return app.exec();
}
