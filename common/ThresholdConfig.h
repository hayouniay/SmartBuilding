#pragma once

// Threshold and automation settings shared by the server (which applies them) and the
// dashboard (which edits them and draws the limits). Stored as JSON in config/thresholds.json.

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <cmath>
#include <expected>
#include <vector>

#include "common/MessageParser.h"   // provides sb::detail::str and sb::detail::num
#include "common/Rules.h"

namespace sb {

struct ThresholdEntry {
    QString name;               // e.g. "temperature-high"
    QString sensorType;         // e.g. "temperature"
    bool above = true;          // true: alert when the value goes above the limit
    double limit = 0.0;
    double hysteresis = 0.0;
    double criticalMargin = 4.0;
};

struct AutomationEntry {
    QString name;               // e.g. "co2-ventilation"
    QString sensorType;         // trigger sensor, e.g. "co2"
    bool above = true;
    double limit = 0.0;
    double hysteresis = 0.0;
    QString targetType;         // actuator switched in the same zone, e.g. "fan"
};

struct ThresholdConfig {
    QList<ThresholdEntry> thresholds;
    QList<AutomationEntry> automation;

    // Limit of a rule by name, or the fallback when the rule is missing.
    double limitOf(const QString& name, double fallback) const {
        for (const ThresholdEntry& e : thresholds) {
            if (e.name == name) return e.limit;
        }
        return fallback;
    }
};

// The built-in rules, used when no config file exists yet.
inline ThresholdConfig defaultThresholdConfig() {
    ThresholdConfig c;
    for (const rules::ThresholdRule& r : rules::defaultThresholds()) {
        ThresholdEntry e;
        e.name = QString::fromStdString(r.name);
        e.sensorType = QString::fromStdString(r.sensorType);
        e.above = r.direction == rules::Direction::Above;
        e.limit = r.limit;
        e.hysteresis = r.hysteresis;
        e.criticalMargin = r.criticalMargin;
        c.thresholds.append(e);
    }
    for (const rules::AutomationRule& r : rules::defaultAutomation()) {
        AutomationEntry e;
        e.name = QString::fromStdString(r.name);
        e.sensorType = QString::fromStdString(r.sensorType);
        e.above = r.direction == rules::Direction::Above;
        e.limit = r.limit;
        e.hysteresis = r.hysteresis;
        e.targetType = QString::fromStdString(r.targetType);
        c.automation.append(e);
    }
    return c;
}

inline std::vector<rules::ThresholdRule> toThresholdRules(const ThresholdConfig& c) {
    std::vector<rules::ThresholdRule> out;
    for (const ThresholdEntry& e : c.thresholds) {
        out.push_back({e.name.toStdString(), e.sensorType.toStdString(),
                       e.above ? rules::Direction::Above : rules::Direction::Below,
                       e.limit, e.hysteresis, e.criticalMargin});
    }
    return out;
}

inline std::vector<rules::AutomationRule> toAutomationRules(const ThresholdConfig& c) {
    std::vector<rules::AutomationRule> out;
    for (const AutomationEntry& e : c.automation) {
        out.push_back({e.name.toStdString(), e.sensorType.toStdString(),
                       e.above ? rules::Direction::Above : rules::Direction::Below,
                       e.limit, e.hysteresis, e.targetType.toStdString()});
    }
    return out;
}

// Rejects settings the server must not apply: missing names, non-numbers, negative bands.
inline std::expected<void, QString> validateThresholdConfig(const ThresholdConfig& c) {
    for (const ThresholdEntry& e : c.thresholds) {
        if (e.name.isEmpty() || e.sensorType.isEmpty())
            return std::unexpected(QStringLiteral("a threshold has no name or sensor type"));
        if (!std::isfinite(e.limit) || !std::isfinite(e.hysteresis) || !std::isfinite(e.criticalMargin))
            return std::unexpected(QStringLiteral("%1: values must be numbers").arg(e.name));
        if (e.hysteresis < 0.0 || e.criticalMargin < 0.0)
            return std::unexpected(QStringLiteral("%1: hysteresis and critical margin cannot be negative").arg(e.name));
    }
    for (const AutomationEntry& e : c.automation) {
        if (e.name.isEmpty() || e.sensorType.isEmpty() || e.targetType.isEmpty())
            return std::unexpected(QStringLiteral("an automation rule has no name, sensor or target"));
        if (!std::isfinite(e.limit) || !std::isfinite(e.hysteresis))
            return std::unexpected(QStringLiteral("%1: values must be numbers").arg(e.name));
        if (e.hysteresis < 0.0)
            return std::unexpected(QStringLiteral("%1: hysteresis cannot be negative").arg(e.name));
    }
    return {};
}

inline QByteArray toJson(const ThresholdConfig& c) {
    QJsonArray thresholds;
    for (const ThresholdEntry& e : c.thresholds) {
        thresholds.append(QJsonObject{{"name", e.name}, {"sensorType", e.sensorType},
                                      {"direction", e.above ? QStringLiteral("above") : QStringLiteral("below")},
                                      {"limit", e.limit}, {"hysteresis", e.hysteresis},
                                      {"criticalMargin", e.criticalMargin}});
    }
    QJsonArray automation;
    for (const AutomationEntry& e : c.automation) {
        automation.append(QJsonObject{{"name", e.name}, {"sensorType", e.sensorType},
                                      {"direction", e.above ? QStringLiteral("above") : QStringLiteral("below")},
                                      {"limit", e.limit}, {"hysteresis", e.hysteresis},
                                      {"targetType", e.targetType}});
    }
    const QJsonObject root{{"thresholds", thresholds}, {"automation", automation}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

inline std::expected<ThresholdConfig, QString> parseThresholdConfig(const QByteArray& data) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return std::unexpected(err.errorString());
    if (!doc.isObject()) return std::unexpected(QStringLiteral("settings must be a JSON object"));

    ThresholdConfig c;
    for (const QJsonValue& v : doc.object().value(QLatin1StringView("thresholds")).toArray()) {
        const QJsonObject o = v.toObject();
        ThresholdEntry e;
        e.name = detail::str(o, "name");
        e.sensorType = detail::str(o, "sensorType");
        e.above = o.value(QLatin1StringView("direction")).toString(QStringLiteral("above")) != QLatin1String("below");
        e.limit = detail::num(o, "limit");
        e.hysteresis = detail::num(o, "hysteresis");
        e.criticalMargin = o.value(QLatin1StringView("criticalMargin")).toDouble(4.0);
        c.thresholds.append(e);
    }
    for (const QJsonValue& v : doc.object().value(QLatin1StringView("automation")).toArray()) {
        const QJsonObject o = v.toObject();
        AutomationEntry e;
        e.name = detail::str(o, "name");
        e.sensorType = detail::str(o, "sensorType");
        e.above = o.value(QLatin1StringView("direction")).toString(QStringLiteral("above")) != QLatin1String("below");
        e.limit = detail::num(o, "limit");
        e.hysteresis = detail::num(o, "hysteresis");
        e.targetType = detail::str(o, "targetType");
        c.automation.append(e);
    }

    if (auto ok = validateThresholdConfig(c); !ok) return std::unexpected(ok.error());
    return c;
}

// Reads a settings file. Returns the built-in defaults if the file does not exist.
inline std::expected<ThresholdConfig, QString> loadThresholdFile(const QString& path) {
    QFile file(path);
    if (!file.exists()) return defaultThresholdConfig();
    if (!file.open(QIODevice::ReadOnly)) return std::unexpected(QStringLiteral("cannot read %1").arg(path));
    return parseThresholdConfig(file.readAll());
}

inline bool saveThresholdFile(const QString& path, const ThresholdConfig& c) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(toJson(c)) >= 0;
}

}  // namespace sb
