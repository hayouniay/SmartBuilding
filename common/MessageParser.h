#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <expected>

namespace sb {

enum class Severity { Info, Warning, Critical };

struct Telemetry {
    QString deviceId;
    QString zone;
    QString type;       // "temperature", "humidity", "co2"
    QString unit;       // "°C", "%RH", "ppm"
    double value = 0.0;
    QDateTime timestamp;
};

struct DeviceStatus {
    QString deviceId;
    QString zone;
    QString type;
    QString firmware;
    bool online = false;
    QDateTime timestamp;
};

struct DeviceState {
    QString deviceId;
    QString zone;
    QString type;       // "hvac", "light", "fan"
    bool powered = false;
    double setpoint = 0.0;
    QDateTime timestamp;
};

struct Command {
    QString action;     // "power" (value 0 or 1) or "setpoint" (value in °C)
    double value = 0.0;
};

struct Alert {
    qint64 id = 0;      // database id, assigned by the server when the alert is stored
    Severity severity = Severity::Info;
    QString zone;
    QString deviceId;
    QString message;
    QDateTime timestamp;
    bool acknowledged = false;
    QString ackBy;
    QDateTime ackAt;
};

// A dashboard asks for an alert to be acknowledged. The server sends back the same
// record, with its own timestamp, once it has accepted it.
struct AlertAck {
    qint64 id = 0;
    QString by;
    QDateTime at;
};

// A dashboard asks for recent history on connect. sinceMs is epoch milliseconds;
// an empty zone means all zones.
struct HistoryRequest {
    QString clientId;
    qint64 sinceMs = 0;
    QString zone;
    qint64 untilMs = 0;   // 0 means now
};

struct HistoryReply {
    QList<Telemetry> telemetry;     // oldest first
    QList<Alert> alerts;            // oldest first
};

inline QString toString(Severity s) {
    switch (s) {
    case Severity::Info:     return QStringLiteral("info");
    case Severity::Warning:  return QStringLiteral("warning");
    case Severity::Critical: return QStringLiteral("critical");
    }
    return QStringLiteral("info");
}

inline Severity severityFromString(const QString& s) {
    if (s == "critical") return Severity::Critical;
    if (s == "warning")  return Severity::Warning;
    return Severity::Info;
}

namespace detail {

inline std::expected<QJsonObject, QString> parseObject(const QByteArray& data) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
        return std::unexpected(err.errorString());
    if (!doc.isObject())
        return std::unexpected(QStringLiteral("payload is not a JSON object"));
    return doc.object();
}

inline QString str(const QJsonObject& o, const char* key)  { return o.value(QLatin1StringView(key)).toString(); }
inline double  num(const QJsonObject& o, const char* key)  { return o.value(QLatin1StringView(key)).toDouble(); }
inline bool    flag(const QJsonObject& o, const char* key) { return o.value(QLatin1StringView(key)).toBool(); }

inline QDateTime readTime(const QJsonObject& o) {
    const QDateTime t = QDateTime::fromString(str(o, "timestamp"), Qt::ISODateWithMs);
    return t.isValid() ? t : QDateTime::currentDateTimeUtc();
}

inline QString iso(const QDateTime& t) { return t.toString(Qt::ISODateWithMs); }

}  // namespace detail

// ---- JSON objects (used directly and inside the history reply) ----

inline QJsonObject toObject(const Telemetry& t) {
    return QJsonObject{{"deviceId", t.deviceId}, {"zone", t.zone}, {"type", t.type},
                       {"unit", t.unit},         {"value", t.value},
                       {"timestamp", detail::iso(t.timestamp)}};
}

inline Telemetry telemetryFromObject(const QJsonObject& o) {
    Telemetry t;
    t.deviceId  = detail::str(o, "deviceId");
    t.zone      = detail::str(o, "zone");
    t.type      = detail::str(o, "type");
    t.unit      = detail::str(o, "unit");
    t.value     = detail::num(o, "value");
    t.timestamp = detail::readTime(o);
    return t;
}

inline QJsonObject toObject(const Alert& a) {
    return QJsonObject{{"id", static_cast<double>(a.id)},
                       {"severity", toString(a.severity)},
                       {"zone", a.zone},
                       {"deviceId", a.deviceId},
                       {"message", a.message},
                       {"timestamp", detail::iso(a.timestamp)},
                       {"acknowledged", a.acknowledged},
                       {"ackBy", a.ackBy},
                       {"ackAt", detail::iso(a.ackAt)}};
}

inline Alert alertFromObject(const QJsonObject& o) {
    Alert a;
    a.id = static_cast<qint64>(detail::num(o, "id"));
    a.severity = severityFromString(detail::str(o, "severity"));
    a.zone = detail::str(o, "zone");
    a.deviceId = detail::str(o, "deviceId");
    a.message = detail::str(o, "message");
    a.timestamp = detail::readTime(o);
    a.acknowledged = detail::flag(o, "acknowledged");
    a.ackBy = detail::str(o, "ackBy");
    a.ackAt = QDateTime::fromString(detail::str(o, "ackAt"), Qt::ISODateWithMs);
    return a;
}

// ---- Telemetry ----

inline std::expected<Telemetry, QString> parseTelemetry(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    Telemetry t = telemetryFromObject(*obj);
    if (t.deviceId.isEmpty()) return std::unexpected(QStringLiteral("missing deviceId"));
    return t;
}

inline QByteArray toJson(const Telemetry& t) {
    return QJsonDocument(toObject(t)).toJson(QJsonDocument::Compact);
}

// ---- Device status (online/offline) ----

inline std::expected<DeviceStatus, QString> parseStatus(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    DeviceStatus s;
    s.deviceId = detail::str(*obj, "deviceId");
    if (s.deviceId.isEmpty()) return std::unexpected(QStringLiteral("missing deviceId"));
    s.zone      = detail::str(*obj, "zone");
    s.type      = detail::str(*obj, "type");
    s.firmware  = detail::str(*obj, "firmware");
    s.online    = detail::flag(*obj, "online");
    s.timestamp = detail::readTime(*obj);
    return s;
}

inline QByteArray toJson(const DeviceStatus& s) {
    const QJsonObject o{{"deviceId", s.deviceId}, {"zone", s.zone}, {"type", s.type},
                        {"firmware", s.firmware}, {"online", s.online},
                        {"timestamp", detail::iso(s.timestamp)}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ---- Actuator state ----

inline std::expected<DeviceState, QString> parseState(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    DeviceState s;
    s.deviceId = detail::str(*obj, "deviceId");
    if (s.deviceId.isEmpty()) return std::unexpected(QStringLiteral("missing deviceId"));
    s.zone      = detail::str(*obj, "zone");
    s.type      = detail::str(*obj, "type");
    s.powered   = detail::flag(*obj, "powered");
    s.setpoint  = detail::num(*obj, "setpoint");
    s.timestamp = detail::readTime(*obj);
    return s;
}

inline QByteArray toJson(const DeviceState& s) {
    const QJsonObject o{{"deviceId", s.deviceId}, {"zone", s.zone}, {"type", s.type},
                        {"powered", s.powered},   {"setpoint", s.setpoint},
                        {"timestamp", detail::iso(s.timestamp)}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ---- Command ----

inline std::expected<Command, QString> parseCommand(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    Command c;
    c.action = detail::str(*obj, "action");
    c.value  = detail::num(*obj, "value");
    if (c.action != "power" && c.action != "setpoint")
        return std::unexpected(QStringLiteral("unknown action: %1").arg(c.action));
    return c;
}

inline QByteArray toJson(const Command& c) {
    const QJsonObject o{{"action", c.action}, {"value", c.value}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ---- Alert ----

inline std::expected<Alert, QString> parseAlert(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    return alertFromObject(*obj);
}

inline QByteArray toJson(const Alert& a) {
    return QJsonDocument(toObject(a)).toJson(QJsonDocument::Compact);
}

// ---- Acknowledgement ----

inline std::expected<AlertAck, QString> parseAlertAck(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    AlertAck ack;
    ack.id = static_cast<qint64>(detail::num(*obj, "id"));
    ack.by = detail::str(*obj, "by");
    ack.at = QDateTime::fromString(detail::str(*obj, "at"), Qt::ISODateWithMs);
    if (ack.id <= 0) return std::unexpected(QStringLiteral("missing alert id"));
    if (ack.by.isEmpty()) ack.by = QStringLiteral("operator");
    if (!ack.at.isValid()) ack.at = QDateTime::currentDateTimeUtc();
    return ack;
}

inline QByteArray toJson(const AlertAck& a) {
    const QJsonObject o{{"id", static_cast<double>(a.id)}, {"by", a.by}, {"at", detail::iso(a.at)}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ---- History ----

inline std::expected<HistoryRequest, QString> parseHistoryRequest(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    HistoryRequest r;
    r.clientId = detail::str(*obj, "clientId");
    r.sinceMs  = static_cast<qint64>(detail::num(*obj, "sinceMs"));
    r.zone     = detail::str(*obj, "zone");
    r.untilMs  = static_cast<qint64>(detail::num(*obj, "untilMs"));
    // The client id becomes part of a topic, so it must not contain topic wildcards or separators.
    if (r.clientId.isEmpty() || r.clientId.contains(u'/') || r.clientId.contains(u'+') || r.clientId.contains(u'#'))
        return std::unexpected(QStringLiteral("invalid clientId"));
    return r;
}

inline QByteArray toJson(const HistoryRequest& r) {
    const QJsonObject o{{"clientId", r.clientId}, {"sinceMs", static_cast<double>(r.sinceMs)},
                        {"zone", r.zone}, {"untilMs", static_cast<double>(r.untilMs)}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

inline std::expected<HistoryReply, QString> parseHistoryReply(const QByteArray& data) {
    auto obj = detail::parseObject(data);
    if (!obj) return std::unexpected(obj.error());
    HistoryReply r;
    for (const QJsonValue& v : obj->value(QLatin1StringView("telemetry")).toArray())
        r.telemetry.append(telemetryFromObject(v.toObject()));
    for (const QJsonValue& v : obj->value(QLatin1StringView("alerts")).toArray())
        r.alerts.append(alertFromObject(v.toObject()));
    return r;
}

inline QByteArray toJson(const HistoryReply& r) {
    QJsonArray telemetry;
    for (const Telemetry& t : r.telemetry) telemetry.append(toObject(t));
    QJsonArray alerts;
    for (const Alert& a : r.alerts) alerts.append(toObject(a));
    const QJsonObject o{{"telemetry", telemetry}, {"alerts", alerts}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

}  // namespace sb
