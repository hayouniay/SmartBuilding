#include "SensorSimulator.h"

#include "common/MqttTopics.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cmath>

using namespace sb;

namespace {

constexpr double kOutdoorC = 31.0;   // a warm day
constexpr double kOutdoorCo2 = 420.0;
constexpr int kTickMs = 2000;

ZoneModel makeZone(const QString& name, double heatLoad, int occupants) {
    ZoneModel z;
    z.name = name;
    z.heatLoad = heatLoad;
    z.occupants = occupants;
    return z;
}

}  // namespace

SensorSimulator::SensorSimulator(QString host, quint16 port, QObject* parent)
    : QObject(parent),
      m_host(std::move(host)),
      m_port(port),
      m_zones{makeZone(QStringLiteral("F1-Lobby"), 0.00, 2),
              makeZone(QStringLiteral("F2-OpenOffice"), 0.02, 8),
              makeZone(QStringLiteral("F2-ServerRoom"), 0.35, 0),   // hot: trips temperature alerts when HVAC is off
              makeZone(QStringLiteral("F3-MeetingRoom"), 0.05, 4)} {
    m_client.setClientId(QStringLiteral("sb-simulator"));

    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &SensorSimulator::tick);

    connect(&m_client, &QMqttClient::stateChanged, this, &SensorSimulator::onStateChanged);
    connect(&m_client, &QMqttClient::messageReceived, this, &SensorSimulator::onMessage);

    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, [this] { m_client.connectToHost(); });
}

void SensorSimulator::start() {
    qInfo() << "Simulator connecting to" << m_host << m_port;
    m_client.setHostname(m_host);
    m_client.setPort(m_port);
    m_client.connectToHost();
}

void SensorSimulator::onStateChanged(QMqttClient::ClientState state) {
    if (state == QMqttClient::Connected) {
        m_backoff.reset();
        qInfo() << "Simulator connected: publishing" << m_zones.size() << "zones";
        m_client.subscribe(QMqttTopicFilter(topics::kCommandFilter), 1);
        announceDevices();
        m_tick.start();
    } else if (state == QMqttClient::Disconnected) {
        m_tick.stop();
        const auto delay = m_backoff.nextDelay();
        qWarning() << "Simulator disconnected; retry" << m_backoff.attempts() << "in" << delay.count() << "ms";
        m_reconnect.start(static_cast<int>(delay.count()));
    }
}

void SensorSimulator::announceDevices() {
    for (const auto& z : m_zones) {
        publishStatus(z.name, QStringLiteral("TS-") + z.name, QStringLiteral("temperature"));
        publishStatus(z.name, QStringLiteral("HS-") + z.name, QStringLiteral("humidity"));
        publishStatus(z.name, QStringLiteral("CO2-") + z.name, QStringLiteral("co2"));
        publishStatus(z.name, QStringLiteral("HVAC-") + z.name, QStringLiteral("hvac"));
        publishStatus(z.name, QStringLiteral("LGT-") + z.name, QStringLiteral("light"));
        publishStatus(z.name, QStringLiteral("FAN-") + z.name, QStringLiteral("fan"));
        publishState(z);
    }
}

void SensorSimulator::tick() {
    std::normal_distribution<double> tempNoise(0.0, 0.05);
    std::normal_distribution<double> humNoise(0.0, 0.3);

    for (auto& z : m_zones) {
        // Temperature: HVAC pulls toward setpoint; otherwise drift toward outdoor.
        if (z.hvacOn)
            z.temperature += std::clamp(z.setpoint - z.temperature, -0.4, 0.4);
        else
            z.temperature += (kOutdoorC - z.temperature) * 0.03;
        z.temperature += z.heatLoad + tempNoise(m_rng);
        z.humidity = std::clamp(z.humidity + humNoise(m_rng), 25.0, 75.0);

        // CO2: occupants add gas, the fan removes it, and it relaxes toward outdoor air.
        z.co2 += z.occupants * 3.0;
        if (z.fanOn) z.co2 -= 40.0;
        z.co2 += (kOutdoorCo2 - z.co2) * 0.01;
        z.co2 = std::clamp(z.co2, 400.0, 2500.0);

        publishTelemetry(z);
    }

    // Battery and signal strength every 15 ticks (30 s), not on every reading.
    if (++m_ticks % 15 == 0) {
        for (const auto& z : m_zones) publishHealth(z);
    }
}

void SensorSimulator::publishHealth(const ZoneModel& z) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    std::uniform_real_distribution<double> signalDist(-82.0, -58.0);
    const QString sensors[] = {
        QStringLiteral("TS-") + z.name,
        QStringLiteral("HS-") + z.name,
        QStringLiteral("CO2-") + z.name,
    };

    for (const QString& id : sensors) {
        // Batteries start at a random level and drain slowly, so low-battery alerts occur in a demo run.
        auto inserted = m_battery.try_emplace(id, 100.0 - static_cast<double>(m_rng() % 88));
        double& level = inserted.first->second;
        level = std::max(0.0, level - 0.4);

        Telemetry battery;
        battery.deviceId = id;
        battery.zone = z.name;
        battery.type = QStringLiteral("battery");
        battery.unit = QStringLiteral("%");
        battery.value = std::round(level);
        battery.timestamp = now;
        publish(topics::telemetry(z.name, id), toJson(battery), 0, false);

        Telemetry rssi;
        rssi.deviceId = id;
        rssi.zone = z.name;
        rssi.type = QStringLiteral("rssi");
        rssi.unit = QStringLiteral("dBm");
        rssi.value = std::round(signalDist(m_rng));
        rssi.timestamp = now;
        publish(topics::telemetry(z.name, id), toJson(rssi), 0, false);
    }
}

void SensorSimulator::publishTelemetry(const ZoneModel& z) {
    const QDateTime now = QDateTime::currentDateTimeUtc();

    Telemetry temp;
    temp.deviceId = QStringLiteral("TS-") + z.name;
    temp.zone = z.name;
    temp.type = QStringLiteral("temperature");
    temp.unit = QStringLiteral("°C");
    temp.value = z.temperature;
    temp.timestamp = now;
    publish(topics::telemetry(z.name, temp.deviceId), toJson(temp), 0, false);

    Telemetry hum;
    hum.deviceId = QStringLiteral("HS-") + z.name;
    hum.zone = z.name;
    hum.type = QStringLiteral("humidity");
    hum.unit = QStringLiteral("%RH");
    hum.value = z.humidity;
    hum.timestamp = now;
    publish(topics::telemetry(z.name, hum.deviceId), toJson(hum), 0, false);

    Telemetry co2;
    co2.deviceId = QStringLiteral("CO2-") + z.name;
    co2.zone = z.name;
    co2.type = QStringLiteral("co2");
    co2.unit = QStringLiteral("ppm");
    co2.value = z.co2;
    co2.timestamp = now;
    publish(topics::telemetry(z.name, co2.deviceId), toJson(co2), 0, false);
}

void SensorSimulator::publishState(const ZoneModel& z) {
    const QDateTime now = QDateTime::currentDateTimeUtc();

    DeviceState hvac;
    hvac.deviceId = QStringLiteral("HVAC-") + z.name;
    hvac.zone = z.name;
    hvac.type = QStringLiteral("hvac");
    hvac.powered = z.hvacOn;
    hvac.setpoint = z.setpoint;
    hvac.timestamp = now;
    publish(topics::state(z.name, hvac.deviceId), toJson(hvac), 1, true);

    DeviceState light;
    light.deviceId = QStringLiteral("LGT-") + z.name;
    light.zone = z.name;
    light.type = QStringLiteral("light");
    light.powered = z.lightOn;
    light.timestamp = now;
    publish(topics::state(z.name, light.deviceId), toJson(light), 1, true);

    DeviceState fan;
    fan.deviceId = QStringLiteral("FAN-") + z.name;
    fan.zone = z.name;
    fan.type = QStringLiteral("fan");
    fan.powered = z.fanOn;
    fan.timestamp = now;
    publish(topics::state(z.name, fan.deviceId), toJson(fan), 1, true);
}

void SensorSimulator::publishStatus(const QString& zone, const QString& id, const QString& type) {
    DeviceStatus s;
    s.deviceId = id;
    s.zone = zone;
    s.type = type;
    s.firmware = QStringLiteral("v2.4.1");
    s.online = true;
    s.timestamp = QDateTime::currentDateTimeUtc();
    publish(topics::status(zone, id), toJson(s), 1, true);
}

void SensorSimulator::onMessage(const QByteArray& payload, const QMqttTopicName& topic) {
    const auto info = topics::parse(topic.name());
    if (!info || info->kind != topics::Kind::Command) return;

    ZoneModel* zone = findZone(info->zone);
    const auto cmd = parseCommand(payload);
    if (!zone || !cmd) {
        qWarning() << "Ignoring command on" << topic.name();
        return;
    }

    const QString& id = info->deviceId;
    if (id.startsWith(QStringLiteral("HVAC-"))) {
        if (cmd->action == "power")
            zone->hvacOn = cmd->value > 0.5;
        else if (cmd->action == "setpoint")
            zone->setpoint = std::clamp(cmd->value, 10.0, 35.0);
    } else if (id.startsWith(QStringLiteral("LGT-")) && cmd->action == "power") {
        zone->lightOn = cmd->value > 0.5;
    } else if (id.startsWith(QStringLiteral("FAN-")) && cmd->action == "power") {
        zone->fanOn = cmd->value > 0.5;
    } else {
        return;
    }

    qInfo().noquote() << "Command applied:" << id << cmd->action << cmd->value;
    publishState(*zone);
}

ZoneModel* SensorSimulator::findZone(const QString& name) {
    const auto it = std::ranges::find(m_zones, name, &ZoneModel::name);
    return it != m_zones.end() ? &*it : nullptr;
}

void SensorSimulator::publish(const QString& topic, const QByteArray& payload, quint8 qos, bool retain) {
    if (m_client.state() != QMqttClient::Connected) return;
    m_client.publish(QMqttTopicName(topic), payload, qos, retain);
}
