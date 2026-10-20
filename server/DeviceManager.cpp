#include "DeviceManager.h"

using namespace sb;
namespace rules = sb::rules;

namespace {

Severity toSeverity(rules::Level level) {
    switch (level) {
    case rules::Level::Info:     return Severity::Info;
    case rules::Level::Warning:  return Severity::Warning;
    case rules::Level::Critical: return Severity::Critical;
    }
    return Severity::Info;
}

}  // namespace

DeviceManager::DeviceManager(const sb::ThresholdConfig& config, QObject* parent)
    : QObject(parent),
      m_thresholds(sb::toThresholdRules(config)),
      m_automation(sb::toAutomationRules(config)) {
    m_watchdog.setInterval(5'000);
    connect(&m_watchdog, &QTimer::timeout, this, &DeviceManager::checkSilence);
    m_watchdog.start();
}

void DeviceManager::applyConfig(const sb::ThresholdConfig& config) {
    m_thresholds = rules::ThresholdEvaluator(sb::toThresholdRules(config));
    m_automation = rules::AutomationEngine(sb::toAutomationRules(config));
}

void DeviceManager::handleTelemetry(const Telemetry& t) {
    DeviceRecord& d = m_devices[t.deviceId];
    d.id = t.deviceId;
    d.zone = t.zone;
    // Battery and signal describe the device's health; they must not change its measurement type.
    if (t.type != "battery" && t.type != "rssi") {
        d.type = t.type;
        d.unit = t.unit;
        d.lastValue = t.value;
    }
    markOnline(d);

    const rules::Reading reading{t.deviceId.toStdString(), t.zone.toStdString(),
                                 t.type.toStdString(), t.value};

    for (const auto& ev : m_thresholds.evaluate(reading)) {
        if (ev.recovered) {
            emitAlert(Severity::Info, d, QStringLiteral("%1 back within limits").arg(d.id));
        } else {
            emitAlert(toSeverity(ev.level), d,
                      QStringLiteral("%1 in %2 reads %3 %4 (rule %5)")
                          .arg(d.id, d.zone, QString::number(ev.value, 'f', 1), d.unit,
                               QString::fromStdString(ev.rule)));
        }
    }

    for (const auto& act : m_automation.evaluate(reading)) {
        const QString zone = QString::fromStdString(act.zone);
        const QString target = QString::fromStdString(act.targetType);
        const QString actuatorId = findActuator(zone, target);
        if (actuatorId.isEmpty()) {
            qWarning() << "Automation" << act.rule << "skipped: no" << target << "in" << zone;
            continue;
        }
        qInfo().noquote() << "Automation" << QString::fromStdString(act.rule)
                          << "->" << actuatorId << (act.powerOn ? "ON" : "OFF");
        emit commandRequested(zone, actuatorId,
                              Command{QStringLiteral("power"), act.powerOn ? 1.0 : 0.0});
    }
}

void DeviceManager::handleStatus(const DeviceStatus& s) {
    DeviceRecord& d = m_devices[s.deviceId];
    const bool wasOnline = d.online;
    d.id = s.deviceId;
    d.zone = s.zone;
    d.type = s.type;
    d.firmware = s.firmware;

    if (s.online) {
        markOnline(d);
    } else {
        d.online = false;
        if (wasOnline) emitAlert(Severity::Warning, d, QStringLiteral("%1 reported offline").arg(d.id));
    }
}

void DeviceManager::handleState(const DeviceState& s) {
    DeviceRecord& d = m_devices[s.deviceId];
    d.id = s.deviceId;
    d.zone = s.zone;
    d.type = s.type;
    d.powered = s.powered;
    d.setpoint = s.setpoint;
}

void DeviceManager::markOnline(DeviceRecord& d) {
    const bool recovered = m_silence.heard(d.id.toStdString(), rules::SilenceDetector::Clock::now());
    d.online = true;
    if (recovered) emitAlert(Severity::Info, d, QStringLiteral("%1 is reporting again").arg(d.id));
}

void DeviceManager::checkSilence() {
    for (const auto& id : m_silence.newlySilent(rules::SilenceDetector::Clock::now())) {
        auto it = m_devices.find(QString::fromStdString(id));
        if (it == m_devices.end() || !it->online) continue;   // already reported offline explicitly
        it->online = false;
        emitAlert(Severity::Warning, *it, QStringLiteral("%1 stopped reporting (missing readings)").arg(it->id));
    }
}

QString DeviceManager::findActuator(const QString& zone, const QString& type) const {
    for (const auto& d : m_devices) {
        if (d.zone == zone && d.type == type && d.online) return d.id;
    }
    return {};
}

void DeviceManager::emitAlert(Severity severity, const DeviceRecord& d, const QString& message) {
    Alert a;
    a.severity = severity;
    a.zone = d.zone;
    a.deviceId = d.id;
    a.message = message;
    a.timestamp = QDateTime::currentDateTimeUtc();
    emit alertRaised(a);
}
