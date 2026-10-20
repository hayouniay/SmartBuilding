#pragma once

#include <QString>
#include <QStringList>
#include <optional>

// Topic layout
//   smartbuilding/<zone>/<deviceId>/telemetry  sensor readings          device  -> broker
//   smartbuilding/<zone>/<deviceId>/status     online/offline, retained device  -> broker
//   smartbuilding/<zone>/<deviceId>/state      actuator state, retained device -> broker
//   smartbuilding/<zone>/<deviceId>/command    control requests         client  -> broker -> device
//   smartbuilding/alerts                       new alerts (with database id) server -> clients
//   smartbuilding/alerts/ack                   acknowledge request       client  -> server
//   smartbuilding/alerts/acked                 accepted acknowledgement  server  -> clients
//   smartbuilding/history/request              ask for recent history    client  -> server
//   smartbuilding/history/reply/<clientId>     history for one client    server  -> client
//
// Zones are slug-like names without spaces, e.g. "F2-ServerRoom".

namespace sb::topics {

inline const QString kRoot           = QStringLiteral("smartbuilding");
inline const QString kAllFilter      = QStringLiteral("smartbuilding/#");
inline const QString kCommandFilter  = QStringLiteral("smartbuilding/+/+/command");
inline const QString kAlerts         = QStringLiteral("smartbuilding/alerts");
inline const QString kAlertAck       = QStringLiteral("smartbuilding/alerts/ack");
inline const QString kAlertAcked     = QStringLiteral("smartbuilding/alerts/acked");
inline const QString kHistoryRequest = QStringLiteral("smartbuilding/history/request");
inline const QString kThresholds    = QStringLiteral("smartbuilding/config/thresholds");      // retained, server -> clients
inline const QString kThresholdsSet = QStringLiteral("smartbuilding/config/thresholds/set");  // dashboard -> server

inline QString historyReply(const QString& clientId) {
    return QStringLiteral("smartbuilding/history/reply/%1").arg(clientId);
}

enum class Kind { Telemetry, Status, State, Command, Unknown };

struct Parsed {
    QString zone;
    QString deviceId;
    Kind kind = Kind::Unknown;
};

inline QString build(const QString& zone, const QString& deviceId, const QString& leaf) {
    return QStringLiteral("%1/%2/%3/%4").arg(kRoot, zone, deviceId, leaf);
}

inline QString telemetry(const QString& zone, const QString& id) { return build(zone, id, QStringLiteral("telemetry")); }
inline QString status(const QString& zone, const QString& id)    { return build(zone, id, QStringLiteral("status")); }
inline QString state(const QString& zone, const QString& id)     { return build(zone, id, QStringLiteral("state")); }
inline QString command(const QString& zone, const QString& id)   { return build(zone, id, QStringLiteral("command")); }

// Splits "smartbuilding/<zone>/<id>/<leaf>" into its parts.
inline std::optional<Parsed> parse(const QString& topic) {
    const QStringList parts = topic.split(u'/');
    if (parts.size() != 4 || parts.first() != kRoot) return std::nullopt;

    Parsed p{parts[1], parts[2], Kind::Unknown};
    if (parts[3] == "telemetry")      p.kind = Kind::Telemetry;
    else if (parts[3] == "status")    p.kind = Kind::Status;
    else if (parts[3] == "state")     p.kind = Kind::State;
    else if (parts[3] == "command")   p.kind = Kind::Command;
    else return std::nullopt;

    return p;
}

}  // namespace sb::topics
