#include "DatabaseManager.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>

using namespace sb;

namespace {

// Same text format as the stored timestamps, so string comparison orders them correctly.
QString isoFromMs(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::utc()).toString(Qt::ISODateWithMs);
}

}  // namespace

DatabaseManager::DatabaseManager(QObject* parent) : QObject(parent) {}

DatabaseManager::~DatabaseManager() {
    {
        QSqlDatabase conn = db();
        if (conn.isOpen()) conn.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

QSqlDatabase DatabaseManager::db() const {
    return QSqlDatabase::database(m_connection, false);
}

bool DatabaseManager::open(const QString& path) {
    QSqlDatabase conn = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    conn.setDatabaseName(path);
    if (!conn.open()) {
        qCritical() << "Cannot open database" << path << conn.lastError().text();
        return false;
    }
    qInfo() << "Database opened:" << path;
    return createSchema();
}

bool DatabaseManager::createSchema() {
    QSqlQuery q(db());
    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS telemetry ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " zone TEXT, device_id TEXT, type TEXT,"
                       " value REAL, unit TEXT, ts TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS alerts ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " severity TEXT, zone TEXT, device_id TEXT,"
                       " message TEXT, ts TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS commands ("
                       " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       " zone TEXT, device_id TEXT, action TEXT,"
                       " value REAL, ts TEXT)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_telemetry_device_ts"
                       " ON telemetry (device_id, ts)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_alerts_ts ON alerts (ts)"),
    };

    for (const auto& sql : statements) {
        if (!q.exec(sql)) {
            qCritical() << "Schema error:" << q.lastError().text();
            return false;
        }
    }

    // Databases created before acknowledgement existed get the new columns here.
    // If a column is already there, SQLite reports an error, which is expected and ignored.
    const QStringList migrations = {
        QStringLiteral("ALTER TABLE alerts ADD COLUMN acknowledged INTEGER NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE alerts ADD COLUMN ack_by TEXT"),
        QStringLiteral("ALTER TABLE alerts ADD COLUMN ack_at TEXT"),
    };
    for (const auto& sql : migrations) q.exec(sql);

    return true;
}

void DatabaseManager::insertTelemetry(const Telemetry& t) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO telemetry (zone, device_id, type, value, unit, ts)"
                             " VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(t.zone);
    q.addBindValue(t.deviceId);
    q.addBindValue(t.type);
    q.addBindValue(t.value);
    q.addBindValue(t.unit);
    q.addBindValue(t.timestamp.toString(Qt::ISODateWithMs));
    if (!q.exec()) qWarning() << "Telemetry insert failed:" << q.lastError().text();
}

void DatabaseManager::insertCommand(const QString& zone, const QString& deviceId, const Command& c) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO commands (zone, device_id, action, value, ts)"
                             " VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(zone);
    q.addBindValue(deviceId);
    q.addBindValue(c.action);
    q.addBindValue(c.value);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!q.exec()) qWarning() << "Command insert failed:" << q.lastError().text();
}

qint64 DatabaseManager::insertAlert(const Alert& a) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("INSERT INTO alerts (severity, zone, device_id, message, ts)"
                             " VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(sb::toString(a.severity));
    q.addBindValue(a.zone);
    q.addBindValue(a.deviceId);
    q.addBindValue(a.message);
    q.addBindValue(a.timestamp.toString(Qt::ISODateWithMs));
    if (!q.exec()) {
        qWarning() << "Alert insert failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::acknowledgeAlert(const AlertAck& ack) {
    QSqlQuery q(db());
    q.prepare(QStringLiteral("UPDATE alerts SET acknowledged = 1, ack_by = ?, ack_at = ?"
                             " WHERE id = ? AND acknowledged = 0 AND severity <> 'info'"));
    q.addBindValue(ack.by);
    q.addBindValue(ack.at.toString(Qt::ISODateWithMs));
    q.addBindValue(ack.id);
    if (!q.exec()) {
        qWarning() << "Acknowledge failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

QList<Alert> DatabaseManager::recentAlerts(qint64 sinceMs, qint64 untilMs, int limit) {
    QList<Alert> out;
    QSqlQuery q(db());
    q.prepare(QStringLiteral("SELECT id, severity, zone, device_id, message, ts,"
                             " acknowledged, ack_by, ack_at FROM alerts"
                             " WHERE ts >= ? AND ts <= ? ORDER BY id DESC LIMIT ?"));
    q.addBindValue(isoFromMs(sinceMs));
    q.addBindValue(isoFromMs(untilMs));
    q.addBindValue(limit);
    if (!q.exec()) {
        qWarning() << "Alert history query failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        Alert a;
        a.id = q.value(0).toLongLong();
        a.severity = severityFromString(q.value(1).toString());
        a.zone = q.value(2).toString();
        a.deviceId = q.value(3).toString();
        a.message = q.value(4).toString();
        a.timestamp = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
        a.acknowledged = q.value(6).toInt() != 0;
        a.ackBy = q.value(7).toString();
        a.ackAt = QDateTime::fromString(q.value(8).toString(), Qt::ISODateWithMs);
        out.append(a);
    }
    std::reverse(out.begin(), out.end());   // query returned newest first
    return out;
}

QList<Telemetry> DatabaseManager::telemetryHistory(const QString& zone, qint64 sinceMs, qint64 untilMs,
                                                   int bucketSeconds) {
    QList<Telemetry> out;
    QSqlQuery q(db());
    // One averaged point per device per bucket keeps long histories small.
    q.prepare(QStringLiteral("SELECT zone, device_id, type, MAX(unit), AVG(value), MAX(ts)"
                             " FROM telemetry"
                             " WHERE ts >= ? AND ts <= ? AND (? = '' OR zone = ?)"
                             " GROUP BY device_id, CAST(strftime('%s', ts) AS INTEGER) / ?"
                             " ORDER BY MAX(ts)"));
    q.addBindValue(isoFromMs(sinceMs));
    q.addBindValue(isoFromMs(untilMs));
    q.addBindValue(zone);
    q.addBindValue(zone);
    q.addBindValue(bucketSeconds);
    if (!q.exec()) {
        qWarning() << "Telemetry history query failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        Telemetry t;
        t.zone = q.value(0).toString();
        t.deviceId = q.value(1).toString();
        t.type = q.value(2).toString();
        t.unit = q.value(3).toString();
        t.value = q.value(4).toDouble();
        t.timestamp = QDateTime::fromString(q.value(5).toString(), Qt::ISODateWithMs);
        out.append(t);
    }
    return out;
}
