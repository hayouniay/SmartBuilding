#include "MqttClient.h"

#include "common/MqttTopics.h"

#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

using namespace sb;

MqttClient::MqttClient(QObject* parent) : QObject(parent) {
    m_clientId = QStringLiteral("sb-client-") + QString::number(QRandomGenerator::global()->generate() % 100000);
    m_client.setClientId(m_clientId);

    connect(&m_client, &QMqttClient::stateChanged, this, &MqttClient::onStateChanged);
    connect(&m_client, &QMqttClient::messageReceived, this, &MqttClient::onMessage);
    connect(&m_client, &QMqttClient::errorChanged, this, [this](QMqttClient::ClientError e) {
        emit errorOccurred(QStringLiteral("MQTT error %1").arg(static_cast<int>(e)));
    });

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this] { m_client.connectToHost(); });
}

void MqttClient::connectTo(const QString& host, quint16 port) {
    m_client.setHostname(host);
    m_client.setPort(port);
    m_client.connectToHost();
}

bool MqttClient::isConnected() const {
    return m_client.state() == QMqttClient::Connected;
}

void MqttClient::sendCommand(const QString& zone, const QString& deviceId, const Command& cmd) {
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("Not connected to the broker"));
        return;
    }
    m_client.publish(QMqttTopicName(topics::command(zone, deviceId)), toJson(cmd), 1, false);
}

void MqttClient::sendAck(qint64 alertId, const QString& by) {
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("Not connected: acknowledgement not sent"));
        return;
    }
    AlertAck ack;
    ack.id = alertId;
    ack.by = by;
    ack.at = QDateTime::currentDateTimeUtc();
    m_client.publish(QMqttTopicName(topics::kAlertAck), toJson(ack), 1, false);
}

void MqttClient::requestHistory(qint64 sinceMs, const QString& zone, qint64 untilMs) {
    if (!isConnected()) return;
    HistoryRequest request;
    request.clientId = m_clientId;
    request.sinceMs = sinceMs;
    request.zone = zone;
    request.untilMs = untilMs;
    m_client.publish(QMqttTopicName(topics::kHistoryRequest), toJson(request), 1, false);
}

void MqttClient::onStateChanged(QMqttClient::ClientState state) {
    const bool connected = state == QMqttClient::Connected;
    if (connected) {
        m_backoff.reset();
        m_client.subscribe(QMqttTopicFilter(topics::kAllFilter), 1);
    } else if (state == QMqttClient::Disconnected) {
        m_reconnect.start(static_cast<int>(m_backoff.nextDelay().count()));
    }
    emit connectionChanged(connected);
}

void MqttClient::onMessage(const QByteArray& payload, const QMqttTopicName& topic) {
    const QString name = topic.name();

    if (name == topics::kAlerts) {
        if (auto a = parseAlert(payload)) emit alertReceived(*a);
        else qWarning() << "Bad alert payload:" << a.error();
        return;
    }
    if (name == topics::kThresholds) {
        if (auto config = parseThresholdConfig(payload)) emit thresholdsReceived(*config);
        else qWarning() << "Bad threshold settings:" << config.error();
        return;
    }
    if (name == topics::kAlertAcked) {
        if (auto ack = parseAlertAck(payload)) emit alertAcknowledged(*ack);
        else qWarning() << "Bad acknowledgement payload:" << ack.error();
        return;
    }
    if (name == topics::historyReply(m_clientId)) {
        if (auto reply = parseHistoryReply(payload)) emit historyReceived(*reply);
        else qWarning() << "Bad history payload:" << reply.error();
        return;
    }

    const auto info = topics::parse(name);
    if (!info) return;

    switch (info->kind) {
    case topics::Kind::Telemetry:
        if (auto t = parseTelemetry(payload)) emit telemetryReceived(*t);
        break;
    case topics::Kind::Status:
        if (auto s = parseStatus(payload)) emit statusReceived(*s);
        break;
    case topics::Kind::State:
        if (auto s = parseState(payload)) emit stateReceived(*s);
        break;
    case topics::Kind::Command:   // other clients' commands: not needed here
    case topics::Kind::Unknown:
        break;
    }
}

void MqttClient::sendThresholds(const sb::ThresholdConfig& config) {
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("Not connected: settings not sent"));
        return;
    }
    m_client.publish(QMqttTopicName(topics::kThresholdsSet), toJson(config), 1, false);
}
