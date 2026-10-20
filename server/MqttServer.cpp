#include "MqttServer.h"

#include "DatabaseManager.h"
#include "DeviceManager.h"
#include "common/MqttTopics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include <algorithm>

using namespace sb;

namespace {

constexpr int kHistoryPointsPerDevice = 500;   // keeps history replies a manageable size
constexpr int kHistoryAlertLimit = 500;

}  // namespace

MqttServer::MqttServer(DeviceManager* devices, DatabaseManager* db, const sb::ThresholdConfig& config,
                       const QString& configPath, QObject* parent)
    : QObject(parent), m_devices(devices), m_db(db), m_config(config), m_configPath(configPath) {
    m_client.setClientId(QStringLiteral("sb-server-") + QString::number(QCoreApplication::applicationPid()));

    connect(&m_client, &QMqttClient::stateChanged, this, &MqttServer::onStateChanged);
    connect(&m_client, &QMqttClient::messageReceived, this, &MqttServer::onMessage);
    connect(&m_client, &QMqttClient::errorChanged, this,
            [](QMqttClient::ClientError e) { qWarning() << "MQTT client error:" << e; });
    connect(m_devices, &DeviceManager::commandRequested, this, &MqttServer::sendCommand);

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this] { m_client.connectToHost(); });
}

void MqttServer::start(const QString& host, quint16 port) {
    qInfo() << "Connecting to broker" << host << port;
    m_client.setHostname(host);
    m_client.setPort(port);
    m_client.connectToHost();
}

void MqttServer::onStateChanged(QMqttClient::ClientState state) {
    if (state == QMqttClient::Connected) {
        m_backoff.reset();
        qInfo() << "Connected to broker, subscribing to" << topics::kAllFilter;
        m_client.subscribe(QMqttTopicFilter(topics::kAllFilter), 1);
        publishThresholds();
    } else if (state == QMqttClient::Disconnected) {
        const auto delay = m_backoff.nextDelay();
        qWarning() << "Broker connection lost; retry" << m_backoff.attempts()
                   << "in" << delay.count() << "ms";
        m_reconnect.start(static_cast<int>(delay.count()));
    }
}

void MqttServer::onMessage(const QByteArray& payload, const QMqttTopicName& topic) {
    // Requests that are not device topics are handled first.
    if (topic.name() == topics::kAlertAck) { handleAck(payload); return; }
    if (topic.name() == topics::kHistoryRequest) { handleHistory(payload); return; }
    if (topic.name() == topics::kThresholdsSet) { handleThresholdSet(payload); return; }

    const auto info = topics::parse(topic.name());
    if (!info) return;  // e.g. our own alert and acknowledgement broadcasts

    switch (info->kind) {
    case topics::Kind::Telemetry:
        if (auto t = parseTelemetry(payload)) {
            m_devices->handleTelemetry(*t);
            m_db->insertTelemetry(*t);
        } else {
            qWarning() << "Bad telemetry on" << topic.name() << t.error();
        }
        break;

    case topics::Kind::Status:
        if (auto s = parseStatus(payload)) {
            m_devices->handleStatus(*s);
        } else {
            qWarning() << "Bad status on" << topic.name() << s.error();
        }
        break;

    case topics::Kind::State:
        if (auto s = parseState(payload)) {
            m_devices->handleState(*s);
        } else {
            qWarning() << "Bad state on" << topic.name() << s.error();
        }
        break;

    case topics::Kind::Command:
        // Every command passes through the broker once, so this is the single
        // place commands are audited (including automation commands we send).
        if (auto c = parseCommand(payload)) {
            m_db->insertCommand(info->zone, info->deviceId, *c);
        } else {
            qWarning() << "Bad command on" << topic.name() << c.error();
        }
        break;

    case topics::Kind::Unknown:
        break;
    }
}

void MqttServer::handleAck(const QByteArray& payload) {
    auto ack = parseAlertAck(payload);
    if (!ack) {
        qWarning() << "Bad acknowledgement:" << ack.error();
        return;
    }

    // The server sets the time, so every dashboard shows the same value.
    AlertAck accepted = *ack;
    accepted.at = QDateTime::currentDateTimeUtc();

    if (!m_db->acknowledgeAlert(accepted)) {
        qInfo() << "Acknowledgement ignored for alert" << accepted.id;
        return;
    }
    qInfo().noquote() << "Alert" << accepted.id << "acknowledged by" << accepted.by;
    m_client.publish(QMqttTopicName(topics::kAlertAcked), toJson(accepted), 1, false);
}

void MqttServer::handleHistory(const QByteArray& payload) {
    auto request = parseHistoryRequest(payload);
    if (!request) {
        qWarning() << "Bad history request:" << request.error();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 until = request->untilMs > 0 ? request->untilMs : now;
    const qint64 since = std::min(request->sinceMs, until);
    const qint64 spanSeconds = std::max<qint64>(1, (until - since) / 1000);
    const int bucketSeconds = static_cast<int>(std::max<qint64>(5, spanSeconds / kHistoryPointsPerDevice));

    HistoryReply reply;
    reply.telemetry = m_db->telemetryHistory(request->zone, since, until, bucketSeconds);
    reply.alerts = m_db->recentAlerts(since, until, kHistoryAlertLimit);

    qInfo().noquote() << "History for" << request->clientId << ":" << reply.telemetry.size()
                      << "points," << reply.alerts.size() << "alerts, bucket" << bucketSeconds << "s";
    m_client.publish(QMqttTopicName(topics::historyReply(request->clientId)), toJson(reply), 1, false);
}

void MqttServer::sendCommand(const QString& zone, const QString& deviceId, const Command& cmd) {
    if (m_client.state() != QMqttClient::Connected) {
        qWarning() << "Cannot send command to" << deviceId << "while disconnected";
        return;
    }
    m_client.publish(QMqttTopicName(topics::command(zone, deviceId)), toJson(cmd), 1, false);
}

void MqttServer::publishAlert(const Alert& input) {
    Alert alert = input;
    alert.id = m_db->insertAlert(input);
    qInfo().noquote() << "ALERT" << alert.id << sb::toString(alert.severity) << alert.message;

    if (m_client.state() == QMqttClient::Connected)
        m_client.publish(QMqttTopicName(topics::kAlerts), toJson(alert), 1, false);
}

void MqttServer::handleThresholdSet(const QByteArray& payload) {
    auto parsed = parseThresholdConfig(payload);
    if (!parsed) {
        qWarning() << "Rejected threshold settings:" << parsed.error();
        return;
    }
    m_config = *parsed;
    if (!saveThresholdFile(m_configPath, m_config))
        qWarning() << "Could not save threshold settings to" << m_configPath;
    m_devices->applyConfig(m_config);
    qInfo() << "Threshold settings updated from a dashboard";
    publishThresholds();
}

void MqttServer::publishThresholds() {
    if (m_client.state() != QMqttClient::Connected) return;
    // Retained, so a dashboard that connects later still receives the current settings.
    m_client.publish(QMqttTopicName(topics::kThresholds), toJson(m_config), 1, true);
}
