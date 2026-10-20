#pragma once

// Floor plan layout: each zone is a rectangle on the plan, in fractions of the
// plan's width and height (0 to 1). Stored as JSON in config/floorplan.json.

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <expected>

namespace sb {

struct FloorRoom {
    QString zone;     // must match the zone name used by the sensors
    QString label;    // shown on the plan; defaults to the zone name
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

inline std::expected<QList<FloorRoom>, QString> parseFloorPlan(const QByteArray& data) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return std::unexpected(err.errorString());
    if (!doc.isObject()) return std::unexpected(QStringLiteral("floor plan must be a JSON object"));

    QList<FloorRoom> rooms;
    for (const QJsonValue& v : doc.object().value(QLatin1StringView("rooms")).toArray()) {
        const QJsonObject o = v.toObject();
        FloorRoom r;
        r.zone = o.value(QLatin1StringView("zone")).toString();
        r.label = o.value(QLatin1StringView("label")).toString();
        r.x = o.value(QLatin1StringView("x")).toDouble();
        r.y = o.value(QLatin1StringView("y")).toDouble();
        r.w = o.value(QLatin1StringView("w")).toDouble();
        r.h = o.value(QLatin1StringView("h")).toDouble();
        if (r.label.isEmpty()) r.label = r.zone;

        if (r.zone.isEmpty()) return std::unexpected(QStringLiteral("a room has no zone"));
        if (r.x < 0.0 || r.y < 0.0 || r.w <= 0.0 || r.h <= 0.0 || r.x + r.w > 1.0001 || r.y + r.h > 1.0001)
            return std::unexpected(QStringLiteral("%1: rectangle must lie inside the plan (0 to 1)").arg(r.zone));
        rooms.append(r);
    }
    return rooms;
}

// A missing file means no floor plan yet, which is not an error.
inline std::expected<QList<FloorRoom>, QString> loadFloorPlanFile(const QString& path) {
    QFile file(path);
    if (!file.exists()) return QList<FloorRoom>{};
    if (!file.open(QIODevice::ReadOnly)) return std::unexpected(QStringLiteral("cannot read %1").arg(path));
    return parseFloorPlan(file.readAll());
}

}  // namespace sb
