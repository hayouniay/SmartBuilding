#pragma once

#include <QMqttClient>
#include <QObject>
#include <QTimer>
#include <map>
#include <random>
#include <vector>

#include "common/Backoff.h"
#include "common/MessageParser.h"

// One simulated room. Physics: HVAC pulls temperature toward its setpoint,
// otherwise the room drifts toward outdoor temperature plus internal heat.
struct ZoneModel {
    QString name;
    double temperature = 24.0;   // °C
    double humidity = 45.0;      // %RH
    double setpoint = 22.0;      // °C
    double heatLoad = 0.0;       // internal gains, °C per tick
    bool hvacOn = true;
    bool lightOn = false;
    double co2 = 450.0;          // ppm
    int occupants = 0;           // people producing CO2
    bool fanOn = false;
};

// Publishes telemetry and retained status/state for every simulated device,
// and reacts to commands arriving on smartbuilding/<zone>/<id>/command.
class SensorSimulator : public QObject {
    Q_OBJECT
public:
    SensorSimulator(QString host, quint16 port, QObject* parent = nullptr);
    void start();

private slots:
    void onStateChanged(QMqttClient::ClientState state);
    void onMessage(const QByteArray& payload, const QMqttTopicName& topic);
    void tick();

private:
    void announceDevices();
    void publishTelemetry(const ZoneModel& z);
    void publishState(const ZoneModel& z);
    void publishHealth(const ZoneModel& z);
    void publishStatus(const QString& zone, const QString& id, const QString& type);
    void publish(const QString& topic, const QByteArray& payload, quint8 qos, bool retain);
    ZoneModel* findZone(const QString& name);

    QString m_host;
    quint16 m_port;
    QMqttClient m_client;
    QTimer m_tick;
    QTimer m_reconnect;
    sb::Backoff m_backoff{std::chrono::milliseconds(1000), std::chrono::milliseconds(30000)};
    std::vector<ZoneModel> m_zones;
    std::mt19937 m_rng{std::random_device{}()};
    unsigned long long m_ticks = 0;
    std::map<QString, double> m_battery;   // remaining battery per sensor, %
};
