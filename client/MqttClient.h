#pragma once

#include <QMqttClient>
#include <QObject>
#include <QTimer>
#include "common/Backoff.h"
#include "common/MessageParser.h"
#include "common/ThresholdConfig.h"

// Thin wrapper around QMqttClient that turns MQTT traffic into typed signals and
// provides send helpers for commands, acknowledgements and history requests.
class MqttClient : public QObject {
    Q_OBJECT
public:
    explicit MqttClient(QObject* parent = nullptr);

    void connectTo(const QString& host, quint16 port);
    bool isConnected() const;
    QString clientId() const { return m_clientId; }

    void sendCommand(const QString& zone, const QString& deviceId, const sb::Command& cmd);
    void sendAck(qint64 alertId, const QString& by);
    void requestHistory(qint64 sinceMs, const QString& zone, qint64 untilMs = 0);
    void sendThresholds(const sb::ThresholdConfig& config);

signals:
    void connectionChanged(bool connected);
    void telemetryReceived(const sb::Telemetry& t);
    void statusReceived(const sb::DeviceStatus& s);
    void stateReceived(const sb::DeviceState& s);
    void alertReceived(const sb::Alert& a);
    void alertAcknowledged(const sb::AlertAck& ack);
    void historyReceived(const sb::HistoryReply& reply);
    void thresholdsReceived(const sb::ThresholdConfig& config);
    void errorOccurred(const QString& message);

private slots:
    void onStateChanged(QMqttClient::ClientState state);
    void onMessage(const QByteArray& payload, const QMqttTopicName& topic);

private:
    QString m_clientId;
    QMqttClient m_client;
    QTimer m_reconnect;
    sb::Backoff m_backoff{std::chrono::milliseconds(1000), std::chrono::milliseconds(30000)};
};
