#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>
#include "common/MessageParser.h"
#include "common/Rules.h"
#include "common/ThresholdConfig.h"

struct DeviceRecord {
    QString id;
    QString zone;
    QString type;
    QString firmware;
    QString unit;
    bool online = false;
    bool powered = false;
    double setpoint = 0.0;
    double lastValue = 0.0;
};

// Keeps the authoritative view of every device. Applies threshold alerts,
// automation rules and silence (missing-reading) detection from common/Rules.h.
class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(const sb::ThresholdConfig& config, QObject* parent = nullptr);

    // Replaces the threshold and automation rules. Alert state restarts from the new limits.
    void applyConfig(const sb::ThresholdConfig& config);

    void handleTelemetry(const sb::Telemetry& t);
    void handleStatus(const sb::DeviceStatus& s);
    void handleState(const sb::DeviceState& s);

signals:
    void alertRaised(const sb::Alert& alert);
    void commandRequested(const QString& zone, const QString& deviceId, const sb::Command& cmd);

private:
    void checkSilence();
    void markOnline(DeviceRecord& d);
    void emitAlert(sb::Severity severity, const DeviceRecord& d, const QString& message);
    QString findActuator(const QString& zone, const QString& type) const;

    QHash<QString, DeviceRecord> m_devices;
    QTimer m_watchdog;
    sb::rules::ThresholdEvaluator m_thresholds;
    sb::rules::AutomationEngine m_automation;
    sb::rules::SilenceDetector m_silence{std::chrono::milliseconds(20'000)};
};
