#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointF>
#include <QStringList>
#include <QTimer>
#include <cmath>

#include "MqttClient.h"
#include "Widgets.h"
#include "ThresholdEditor.h"
#include "Theme.h"
#include "common/FloorPlanConfig.h"

class QComboBox;
class QDateEdit;
class QSystemTrayIcon;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;

// Latest known state of one device.
struct DeviceEntry {
    QString id;
    QString zone;
    QString type;
    QString firmware;
    QString unit;
    bool online = false;
    bool powered = false;
    double setpoint = 0.0;
    double value = std::nan("");
    double battery = std::nan("");
    double rssi = std::nan("");
    QDateTime updated;
};

// Latest readings, equipment state and histories for one zone.
struct ZoneEntry {
    double temperature = std::nan("");
    double humidity = std::nan("");
    double co2 = std::nan("");
    bool hvacOn = false;
    bool fanOn = false;
    bool lightOn = false;
    double setpoint = std::nan("");
    QList<QPointF> tempHistory;
    QList<QPointF> humHistory;
    QList<QPointF> co2History;
};

// One alert shown in the Alerts tab. Acknowledgement is tracked here in the client.
struct AlertEntry {
    quint64 id = 0;
    QDateTime time;
    sb::Severity severity = sb::Severity::Info;
    QString zone;
    QString deviceId;
    QString message;
    bool acknowledged = false;
    QString ackBy;
    QDateTime ackAt;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(const QString& host, quint16 port, QWidget* parent = nullptr);

private slots:
    void onConnectionChanged(bool connected);
    void onTelemetry(const sb::Telemetry& t);
    void onStatus(const sb::DeviceStatus& s);
    void onState(const sb::DeviceState& s);
    void onAlert(const sb::Alert& a);
    void onAlertAcknowledged(const sb::AlertAck& ack);
    void onHistoryReceived(const sb::HistoryReply& reply);
    void onZoneFilterChanged(int index);
    void onAlertFilterChanged();
    void onDeviceSelected();
    void onTogglePower();
    void onApplySetpoint();
    void onAcknowledgeSelected();
    void onAcknowledgeAll();
    void onClockTick();
    void onRangeChanged(int index);
    void onDeviceTypeFilterChanged(int index);
    void onThresholdsReceived(const sb::ThresholdConfig& config);
    void onToggleSound();
    void onToggleTheme();
    void onExportTrends();
    void onExportAlerts();
    void onExportReport();

private:
    void buildUi();
    QWidget* buildOverviewTab();
    QWidget* buildDevicesTab();
    QWidget* buildAlertsTab();

    void ensureZone(const QString& zone);
    void setZoneFilter(const QString& zone);
    void refreshDeviceRow(const DeviceEntry& d);
    void applyDeviceFilter();
    void rebuildAlertTable();
    void markAcknowledged(const QList<quint64>& ids);
    bool alertMatchesFilters(const AlertEntry& a) const;
    int activeAlerts(const QString& zone, bool criticalOnly = false) const;
    QString zoneLevel(const QString& zone) const;
    void updateZoneCard(const QString& zone);
    void updateTrends();
    void refreshOverview();
    void updateControls();
    void requestSelectedRange();
    QWidget* buildFloorPlanTab();
    QWidget* buildSettingsTab();
    void applyTheme();
    void updateFloorPlan();
    void updateTrendLimits();
    void notifyCritical(const QString& message);
    void requestHistoryRange(qint64 sinceMs, qint64 untilMs);
    bool deviceVisible(const DeviceEntry& d) const;

    MqttClient m_mqtt;
    QTimer m_clockTimer;
    QString m_brokerText;
    QString m_zoneFilter;                 // empty means all zones
    QStringList m_zoneOrder;
    QHash<QString, DeviceEntry> m_devices;
    QHash<QString, ZoneEntry> m_zones;
    QHash<QString, int> m_rowByDevice;
    QList<AlertEntry> m_alerts;
    quint64 m_alertSeq = 0;
    QString m_selectedDevice;
    QHash<QString, ZoneCard*> m_zoneCards;  // key "" is the building overview card
    int m_zoneCardCount = 0;

    QTabWidget* m_tabs = nullptr;
    QComboBox* m_zoneCombo = nullptr;
    QLabel* m_connPill = nullptr;
    QLabel* m_clock = nullptr;
    QLabel* m_statusLabel = nullptr;
    QGridLayout* m_zoneGrid = nullptr;
    QLabel* m_trendTitle = nullptr;

    KpiCard* m_kpiTemp = nullptr;
    KpiCard* m_kpiHum = nullptr;
    KpiCard* m_kpiCo2 = nullptr;
    KpiCard* m_kpiDevices = nullptr;
    KpiCard* m_kpiAlerts = nullptr;
    TrendChartWidget* m_tempTrend = nullptr;
    TrendChartWidget* m_humTrend = nullptr;
    TrendChartWidget* m_co2Trend = nullptr;

    QTableWidget* m_deviceTable = nullptr;
    QLabel* m_controlTitle = nullptr;
    QLabel* m_controlDetail = nullptr;
    QPushButton* m_powerButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QDoubleSpinBox* m_setpoint = nullptr;

    QTableWidget* m_alertTable = nullptr;
    QComboBox* m_alertSeverity = nullptr;
    QPushButton* m_ackSelected = nullptr;
    QPushButton* m_ackAll = nullptr;
    QComboBox* m_rangeCombo = nullptr;
    QDateEdit* m_fromDate = nullptr;
    QDateEdit* m_toDate = nullptr;
    QPushButton* m_applyRange = nullptr;
    QComboBox* m_deviceTypeFilter = nullptr;
    sb::ThresholdConfig m_config = sb::defaultThresholdConfig();
    ThresholdEditor* m_thresholdEditor = nullptr;
    FloorPlanWidget* m_floorPlan = nullptr;
    QList<sb::FloorRoom> m_floorRooms;
    QWidget* m_devicesPage = nullptr;
    QWidget* m_alertsPage = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QPushButton* m_soundButton = nullptr;
    QPushButton* m_themeButton = nullptr;
    QPushButton* m_exportTrends = nullptr;
    QPushButton* m_exportAlerts = nullptr;
    QPushButton* m_exportReport = nullptr;
    bool m_soundOn = true;
    bool m_lightTheme = false;
    Palette m_palette = darkPalette();
};
