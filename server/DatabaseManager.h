#pragma once

#include <QObject>
#include <QSqlDatabase>
#include "common/MessageParser.h"

// Persists readings, alerts (with acknowledgement) and control commands to SQLite,
// and answers history queries for dashboards.
class DatabaseManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    bool open(const QString& path);

    void insertTelemetry(const sb::Telemetry& t);
    void insertCommand(const QString& zone, const QString& deviceId, const sb::Command& c);

    // Returns the new alert id, or -1 if the insert failed.
    qint64 insertAlert(const sb::Alert& a);

    // True only when the alert existed, was not yet acknowledged, and is not informational.
    bool acknowledgeAlert(const sb::AlertAck& ack);

    // Alerts between sinceMs and untilMs, oldest first.
    QList<sb::Alert> recentAlerts(qint64 sinceMs, qint64 untilMs, int limit);

    // Telemetry since sinceMs, averaged into buckets of bucketSeconds per device, oldest first.
    QList<sb::Telemetry> telemetryHistory(const QString& zone, qint64 sinceMs, qint64 untilMs, int bucketSeconds);

private:
    bool createSchema();
    QSqlDatabase db() const;

    const QString m_connection = QStringLiteral("smartbuilding");
};
