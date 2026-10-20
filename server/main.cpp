#include <QCommandLineParser>
#include <QCoreApplication>

#include "DatabaseManager.h"
#include "DeviceManager.h"
#include "MqttServer.h"
#include "common/ThresholdConfig.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sb-server"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption hostOpt({"b", "broker"}, "MQTT broker host", "host", "localhost");
    const QCommandLineOption portOpt({"p", "port"}, "MQTT broker port", "port", "1883");
    const QCommandLineOption dbOpt({"d", "database"}, "SQLite database file", "path", "smartbuilding.db");
    const QCommandLineOption thresholdsOpt({"t", "thresholds"}, "Threshold settings file", "path",
                                           "config/thresholds.json");
    parser.addOptions({hostOpt, portOpt, dbOpt, thresholdsOpt});
    parser.process(app);

    const QString thresholdsPath = parser.value(thresholdsOpt);
    auto settings = sb::loadThresholdFile(thresholdsPath);
    if (!settings) {
        qCritical() << "Threshold settings are invalid:" << settings.error();
        return 1;
    }

    DatabaseManager db;
    if (!db.open(parser.value(dbOpt))) return 1;

    DeviceManager devices(*settings);
    MqttServer server(&devices, &db, *settings, thresholdsPath);

    QObject::connect(&devices, &DeviceManager::alertRaised, &server, &MqttServer::publishAlert);

    server.start(parser.value(hostOpt), parser.value(portOpt).toUShort());
    return app.exec();
}
