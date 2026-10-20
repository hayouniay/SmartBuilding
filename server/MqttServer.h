#pragma once

#include <QMqttClient>
#include <QObject>
#include <QTimer>
#include "common/Backoff.h"
#include "common/MessageParser.h"
#include "common/ThresholdConfig.h"

class DeviceManager;
class DatabaseManager;

// Broker-side service: subscribes to every smartbuilding topic, feeds the device
// registry and database, publishes alerts, answers acknowledgements and history requests.
class MqttServer : public QObject {
    Q_OBJECT
public:
    MqttServer(DeviceManager* devices, DatabaseManager* db, const sb::ThresholdConfig& config,
               const QString& configPath, QObject* parent = nullptr);
    void start(const QString& host, quint16 port);

public slots:
    void publishAlert(const sb::Alert& alert);

private slots:
    void onStateChanged(QMqttClient::ClientState state);
    void onMessage(const QByteArray& payload, const QMqttTopicName& topic);
    void sendCommand(const QString& zone, const QString& deviceId, const sb::Command& cmd);

private:
    void handleAck(const QByteArray& payload);
    void handleHistory(const QByteArray& payload);
    void handleThresholdSet(const QByteArray& payload);
    void publishThresholds();

    QMqttClient m_client;
    QTimer m_reconnect;
    sb::Backoff m_backoff{std::chrono::milliseconds(1000), std::chrono::milliseconds(30000)};
    DeviceManager* m_devices;
    DatabaseManager* m_db;
    sb::ThresholdConfig m_config;
    QString m_configPath;
};
