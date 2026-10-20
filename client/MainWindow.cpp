#include "MainWindow.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDateTime>
#include <QProcessEnvironment>
#include <QTime>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QSettings>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QDate>
#include "Reports.h"

#include <algorithm>
#include <utility>

using namespace sb;

namespace {

constexpr int kMaxTrendPoints = 600;
constexpr int kMaxAlerts = 500;
constexpr int kZoneColumns = 4;
constexpr double kTempHigh = 26.0;
constexpr double kHumHigh = 60.0;
constexpr double kCo2High = 1000.0;

// Dark operations-console theme. Object names and "level"/"selected" properties
// are used by the widgets in Widgets.h.
QString onOff(bool on) {
    return on ? QStringLiteral("ON") : QStringLiteral("OFF");
}

QString describeValue(const DeviceEntry& d) {
    if (d.type == "hvac")
        return QStringLiteral("%1 | setpoint %2 °C").arg(onOff(d.powered), QString::number(d.setpoint, 'f', 1));
    if (d.type == "light" || d.type == "fan")
        return onOff(d.powered);
    if (std::isnan(d.value))
        return QStringLiteral("--");
    return QStringLiteral("%1 %2").arg(QString::number(d.value, 'f', 1), d.unit);
}

void appendPoint(QList<QPointF>& series, double x, double y) {
    series.append(QPointF(x, y));
    while (series.size() > kMaxTrendPoints) series.removeFirst();
}

QString severityLabel(Severity s) {
    switch (s) {
    case Severity::Critical: return QStringLiteral("Critical");
    case Severity::Warning:  return QStringLiteral("Warning");
    case Severity::Info:     break;
    }
    return QStringLiteral("Info");
}

QColor severityColor(Severity s) {
    switch (s) {
    case Severity::Critical: return QColor("#ef4444");
    case Severity::Warning:  return QColor("#f59e0b");
    case Severity::Info:     break;
    }
    return QColor("#3b82f6");
}

// Preset ranges offered in the toolbar: last hour, 6 hours, 24 hours, 7 days.
constexpr qint64 kRangeMs[] = {3600000LL, 6LL * 3600000LL, 24LL * 3600000LL, 7LL * 24LL * 3600000LL,
                              30LL * 24LL * 3600000LL};
constexpr int kCustomRangeIndex = 5;

constexpr qint64 kHistoryWindowMs = 6LL * 60 * 60 * 1000;   // history requested on connect: last 6 hours

// The Windows user name, or the login name on Linux, recorded with each acknowledgement.
QString operatorName() {
    const QString user = QProcessEnvironment::systemEnvironment().value(QStringLiteral("USERNAME"));
    if (!user.isEmpty()) return user;
    const QString login = QProcessEnvironment::systemEnvironment().value(QStringLiteral("USER"));
    return login.isEmpty() ? QStringLiteral("operator") : login;
}

// Trend history list for one telemetry type of a zone.
QList<QPointF>& historyFor(ZoneEntry& z, const QString& type) {
    if (type == "humidity") return z.humHistory;
    if (type == "co2") return z.co2History;
    return z.tempHistory;
}

QString pillStyle(const QString& color) {
    return QStringLiteral("background:%1; color:#0b1220; border-radius:11px; padding:4px 12px; font-weight:700;")
        .arg(color);
}

QTableWidgetItem* makeItem(const QString& text, const QColor& color) {
    auto* item = new QTableWidgetItem(text);
    item->setForeground(color);
    return item;
}

}  // namespace

MainWindow::MainWindow(const QString& host, quint16 port, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("SmartBuilding Monitor"));
    resize(1440, 900);
    m_brokerText = QStringLiteral("%1:%2").arg(host, QString::number(port));
    buildUi();

    connect(&m_mqtt, &MqttClient::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(&m_mqtt, &MqttClient::telemetryReceived, this, &MainWindow::onTelemetry);
    connect(&m_mqtt, &MqttClient::statusReceived, this, &MainWindow::onStatus);
    connect(&m_mqtt, &MqttClient::stateReceived, this, &MainWindow::onState);
    connect(&m_mqtt, &MqttClient::alertReceived, this, &MainWindow::onAlert);
    connect(&m_mqtt, &MqttClient::alertAcknowledged, this, &MainWindow::onAlertAcknowledged);
    connect(&m_mqtt, &MqttClient::historyReceived, this, &MainWindow::onHistoryReceived);
    connect(&m_mqtt, &MqttClient::thresholdsReceived, this, &MainWindow::onThresholdsReceived);
    connect(&m_mqtt, &MqttClient::errorOccurred, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 4000);
    });

    connect(&m_clockTimer, &QTimer::timeout, this, &MainWindow::onClockTick);
    m_clockTimer.start(1000);
    onClockTick();

    refreshOverview();
    m_mqtt.connectTo(host, port);
}

void MainWindow::buildUi() {
    auto* root = new QWidget;
    root->setObjectName("root");
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- Header bar ----
    auto* header = new QFrame;
    header->setObjectName("header");
    auto* hl = new QHBoxLayout(header);
    hl->setContentsMargins(20, 12, 20, 12);
    hl->setSpacing(12);

    auto* brand = new QVBoxLayout;
    brand->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("SmartBuilding Monitor"));
    title->setObjectName("appTitle");
    auto* subtitle = new QLabel(QStringLiteral("Real-time building operations"));
    subtitle->setObjectName("muted");
    brand->addWidget(title);
    brand->addWidget(subtitle);
    hl->addLayout(brand);
    hl->addStretch(1);

    auto* zoneLabel = new QLabel(QStringLiteral("Zone"));
    zoneLabel->setObjectName("muted");
    m_zoneCombo = new QComboBox;
    m_zoneCombo->setMinimumWidth(220);
    m_zoneCombo->addItem(QStringLiteral("All zones"));
    connect(m_zoneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onZoneFilterChanged);

    auto* rangeLabel = new QLabel(QStringLiteral("Range"));
    rangeLabel->setObjectName("muted");
    m_rangeCombo = new QComboBox;
    m_rangeCombo->addItems({QStringLiteral("Last hour"), QStringLiteral("Last 6 hours"),
                            QStringLiteral("Last 24 hours"), QStringLiteral("Last 7 days"), QStringLiteral("Last 30 days"),
                            QStringLiteral("Custom dates")});
    m_rangeCombo->setCurrentIndex(1);
    connect(m_rangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onRangeChanged);
    m_fromDate = new QDateEdit(QDate::currentDate().addDays(-7));
    m_toDate = new QDateEdit(QDate::currentDate());
    for (QDateEdit* edit : {m_fromDate, m_toDate}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        edit->setVisible(false);
    }
    m_applyRange = new QPushButton(QStringLiteral("Apply"));
    m_applyRange->setObjectName("ghost");
    m_applyRange->setVisible(false);
    connect(m_applyRange, &QPushButton::clicked, this, &MainWindow::requestSelectedRange);

    m_soundButton = new QPushButton(QStringLiteral("Sound: on"));
    m_soundButton->setObjectName("ghost");
    connect(m_soundButton, &QPushButton::clicked, this, &MainWindow::onToggleSound);
    m_themeButton = new QPushButton(QStringLiteral("Light theme"));
    m_themeButton->setObjectName("ghost");
    connect(m_themeButton, &QPushButton::clicked, this, &MainWindow::onToggleTheme);

    m_connPill = new QLabel;
    m_clock = new QLabel;
    m_clock->setObjectName("clock");

    hl->addWidget(zoneLabel);
    hl->addWidget(m_zoneCombo);
    hl->addWidget(rangeLabel);
    hl->addWidget(m_rangeCombo);
    hl->addWidget(m_fromDate);
    hl->addWidget(m_toDate);
    hl->addWidget(m_applyRange);
    hl->addWidget(m_soundButton);
    hl->addWidget(m_themeButton);
    hl->addSpacing(12);
    hl->addWidget(m_connPill);
    hl->addWidget(m_clock);
    rootLayout->addWidget(header);

    // ---- Tabs ----
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(buildOverviewTab(), QStringLiteral("Overview"));
    m_tabs->addTab(buildFloorPlanTab(), QStringLiteral("Floor plan"));
    m_devicesPage = buildDevicesTab();
    m_tabs->addTab(m_devicesPage, QStringLiteral("Devices"));
    m_alertsPage = buildAlertsTab();
    m_tabs->addTab(m_alertsPage, QStringLiteral("Alerts"));
    m_tabs->addTab(buildSettingsTab(), QStringLiteral("Settings"));
    rootLayout->addWidget(m_tabs, 1);

    setCentralWidget(root);

    m_statusLabel = new QLabel;
    statusBar()->addPermanentWidget(m_statusLabel);

    m_lightTheme = QSettings(QStringLiteral("SmartBuilding"), QStringLiteral("Monitor"))
                       .value(QStringLiteral("theme")).toString() == QLatin1String("light");
    applyTheme();

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning), this);
        m_tray->setToolTip(QStringLiteral("SmartBuilding Monitor"));
        m_tray->show();
    }
    updateControls();
    onConnectionChanged(false);
}

QWidget* MainWindow::buildOverviewTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(16);

    // KPI row
    auto* kpis = new QHBoxLayout;
    kpis->setSpacing(14);
    m_kpiTemp = new KpiCard(QStringLiteral("Avg temperature"));
    m_kpiHum = new KpiCard(QStringLiteral("Avg humidity"));
    m_kpiCo2 = new KpiCard(QStringLiteral("Peak CO2"));
    m_kpiDevices = new KpiCard(QStringLiteral("Devices online"));
    m_kpiAlerts = new KpiCard(QStringLiteral("Active alerts"));
    for (KpiCard* k : {m_kpiTemp, m_kpiHum, m_kpiCo2, m_kpiDevices, m_kpiAlerts})
        kpis->addWidget(k, 1);
    lay->addLayout(kpis);

    // Zone cards. The first card is the building overview; zone cards are added as zones appear.
    auto* zonesTitle = new QLabel(QStringLiteral("Zones"));
    zonesTitle->setObjectName("sectionTitle");
    lay->addWidget(zonesTitle);

    auto* zoneHost = new QWidget;
    m_zoneGrid = new QGridLayout(zoneHost);
    m_zoneGrid->setContentsMargins(0, 0, 0, 0);
    m_zoneGrid->setSpacing(14);
    lay->addWidget(zoneHost);

    auto* overview = new ZoneCard(QStringLiteral("Building overview"), [this] { setZoneFilter(QString()); });
    m_zoneGrid->addWidget(overview, 0, 0);
    m_zoneCards.insert(QString(), overview);
    m_zoneCardCount = 1;

    // Trends for the selected zone
    m_trendTitle = new QLabel;
    m_trendTitle->setObjectName("sectionTitle");
    auto* trendBar = new QHBoxLayout;
    trendBar->addWidget(m_trendTitle, 1);
    m_exportTrends = new QPushButton(QStringLiteral("Export trends CSV"));
    m_exportTrends->setObjectName("ghost");
    m_exportReport = new QPushButton(QStringLiteral("Report (loaded range)"));
    m_exportReport->setObjectName("ghost");
    connect(m_exportTrends, &QPushButton::clicked, this, &MainWindow::onExportTrends);
    connect(m_exportReport, &QPushButton::clicked, this, &MainWindow::onExportReport);
    trendBar->addWidget(m_exportTrends);
    trendBar->addWidget(m_exportReport);
    lay->addLayout(trendBar);

    auto* trends = new QHBoxLayout;
    trends->setSpacing(14);
    m_tempTrend = new TrendChartWidget(QStringLiteral("Temperature"), QStringLiteral("°C"), QColor("#22d3ee"));
    m_tempTrend->setThreshold(kTempHigh, QStringLiteral("limit %1 °C").arg(kTempHigh));
    m_humTrend = new TrendChartWidget(QStringLiteral("Humidity"), QStringLiteral("%RH"), QColor("#3b82f6"));
    m_humTrend->setThreshold(kHumHigh, QStringLiteral("limit %1 %RH").arg(kHumHigh));
    m_co2Trend = new TrendChartWidget(QStringLiteral("CO2"), QStringLiteral("ppm"), QColor("#a78bfa"));
    m_co2Trend->setThreshold(kCo2High, QStringLiteral("limit %1 ppm").arg(kCo2High));
    trends->addWidget(m_tempTrend, 1);
    trends->addWidget(m_humTrend, 1);
    trends->addWidget(m_co2Trend, 1);
    lay->addLayout(trends, 1);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::buildDevicesTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(14);

    m_deviceTable = new QTableWidget(0, 9);
    m_deviceTable->setHorizontalHeaderLabels({QStringLiteral("Device ID"), QStringLiteral("Type"),
                                              QStringLiteral("Zone"), QStringLiteral("Status"),
                                              QStringLiteral("Firmware"), QStringLiteral("Value"),
                                              QStringLiteral("Last updated")});
    m_deviceTable->setHorizontalHeaderLabels({QStringLiteral("Device ID"), QStringLiteral("Type"),
                                              QStringLiteral("Zone"), QStringLiteral("Status"),
                                              QStringLiteral("Firmware"), QStringLiteral("Value"),
                                              QStringLiteral("Battery"), QStringLiteral("Signal"),
                                              QStringLiteral("Last updated")});
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setAlternatingRowColors(true);
    m_deviceTable->verticalHeader()->setVisible(false);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onDeviceSelected);
    auto* filterBar = new QHBoxLayout;
    auto* typeLabel = new QLabel(QStringLiteral("Type"));
    typeLabel->setObjectName("muted");
    m_deviceTypeFilter = new QComboBox;
    m_deviceTypeFilter->addItem(QStringLiteral("All types"), QString());
    m_deviceTypeFilter->addItem(QStringLiteral("Temperature"), QStringLiteral("temperature"));
    m_deviceTypeFilter->addItem(QStringLiteral("Humidity"), QStringLiteral("humidity"));
    m_deviceTypeFilter->addItem(QStringLiteral("CO2"), QStringLiteral("co2"));
    m_deviceTypeFilter->addItem(QStringLiteral("HVAC"), QStringLiteral("hvac"));
    m_deviceTypeFilter->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_deviceTypeFilter->addItem(QStringLiteral("Fan"), QStringLiteral("fan"));
    connect(m_deviceTypeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDeviceTypeFilterChanged);
    filterBar->addWidget(typeLabel);
    filterBar->addWidget(m_deviceTypeFilter);
    filterBar->addStretch(1);
    lay->addLayout(filterBar);
    lay->addWidget(m_deviceTable, 1);

    // Control panel for the selected device
    auto* panel = new QFrame;
    panel->setObjectName("panel");
    auto* pl = new QGridLayout(panel);
    pl->setContentsMargins(18, 14, 18, 14);
    pl->setHorizontalSpacing(12);
    pl->setVerticalSpacing(4);

    m_controlTitle = new QLabel;
    m_controlTitle->setObjectName("sectionTitle");
    m_controlDetail = new QLabel;
    m_controlDetail->setObjectName("muted");
    pl->addWidget(m_controlTitle, 0, 0);
    pl->addWidget(m_controlDetail, 1, 0);
    pl->setColumnStretch(0, 1);

    m_powerButton = new QPushButton(QStringLiteral("Turn on"));
    auto* setpointLabel = new QLabel(QStringLiteral("Setpoint"));
    setpointLabel->setObjectName("muted");
    m_setpoint = new QDoubleSpinBox;
    m_setpoint->setRange(10.0, 35.0);
    m_setpoint->setDecimals(1);
    m_setpoint->setSingleStep(0.5);
    m_setpoint->setSuffix(QStringLiteral(" °C"));
    m_setpoint->setValue(22.0);
    m_applyButton = new QPushButton(QStringLiteral("Apply setpoint"));

    pl->addWidget(m_powerButton, 0, 1);
    pl->addWidget(setpointLabel, 0, 2);
    pl->addWidget(m_setpoint, 0, 3);
    pl->addWidget(m_applyButton, 0, 4);
    connect(m_powerButton, &QPushButton::clicked, this, &MainWindow::onTogglePower);
    connect(m_applyButton, &QPushButton::clicked, this, &MainWindow::onApplySetpoint);

    lay->addWidget(panel);
    return page;
}

QWidget* MainWindow::buildAlertsTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);

    auto* bar = new QHBoxLayout;
    auto* severityLabelWidget = new QLabel(QStringLiteral("Severity"));
    severityLabelWidget->setObjectName("muted");
    m_alertSeverity = new QComboBox;
    m_alertSeverity->addItems({QStringLiteral("All"), QStringLiteral("Critical"),
                               QStringLiteral("Warning"), QStringLiteral("Info")});
    connect(m_alertSeverity, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onAlertFilterChanged);

    m_ackSelected = new QPushButton(QStringLiteral("Acknowledge selected"));
    m_ackSelected->setObjectName("ghost");
    m_ackAll = new QPushButton(QStringLiteral("Acknowledge all shown"));
    m_ackAll->setObjectName("ghost");
    connect(m_ackSelected, &QPushButton::clicked, this, &MainWindow::onAcknowledgeSelected);
    connect(m_ackAll, &QPushButton::clicked, this, &MainWindow::onAcknowledgeAll);

    bar->addWidget(severityLabelWidget);
    bar->addWidget(m_alertSeverity);
    bar->addStretch(1);
    bar->addWidget(m_ackSelected);
    m_exportAlerts = new QPushButton(QStringLiteral("Export CSV"));
    m_exportAlerts->setObjectName("ghost");
    connect(m_exportAlerts, &QPushButton::clicked, this, &MainWindow::onExportAlerts);
    bar->addWidget(m_ackAll);
    bar->addWidget(m_exportAlerts);
    lay->addLayout(bar);

    m_alertTable = new QTableWidget(0, 6);
    m_alertTable->setHorizontalHeaderLabels({QStringLiteral("Time"), QStringLiteral("Severity"),
                                             QStringLiteral("Zone"), QStringLiteral("Device"),
                                             QStringLiteral("Message"), QStringLiteral("State")});
    m_alertTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alertTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_alertTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_alertTable->setAlternatingRowColors(true);
    m_alertTable->verticalHeader()->setVisible(false);
    m_alertTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_alertTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    lay->addWidget(m_alertTable, 1);

    return page;
}

// ---------------------------------------------------------------- connection and clock

void MainWindow::onConnectionChanged(bool connected) {
    m_connPill->setStyleSheet(pillStyle(connected ? QStringLiteral("#22c55e") : QStringLiteral("#ef4444")));
    m_connPill->setText(connected ? QStringLiteral("CONNECTED") : QStringLiteral("RECONNECTING"));
    statusBar()->showMessage(connected ? QStringLiteral("Connected to broker %1").arg(m_brokerText)
                                       : QStringLiteral("Broker unavailable, retrying..."), 5000);
    if (connected) {
        // Ask the server for the last few hours so trends and alerts are filled in straight away.
        requestSelectedRange();
    }
}

void MainWindow::onClockTick() {
    m_clock->setText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
}

// ---------------------------------------------------------------- MQTT data

void MainWindow::ensureZone(const QString& zone) {
    if (m_zones.contains(zone)) return;

    m_zones.insert(zone, ZoneEntry{});
    m_zoneOrder.append(zone);
    m_zoneCombo->addItem(zone);

    auto* card = new ZoneCard(zone, [this, zone] { setZoneFilter(zone); });
    m_zoneGrid->addWidget(card, m_zoneCardCount / kZoneColumns, m_zoneCardCount % kZoneColumns);
    ++m_zoneCardCount;
    m_zoneCards.insert(zone, card);
}

void MainWindow::onTelemetry(const Telemetry& t) {
    ensureZone(t.zone);

    // Battery and signal update the device's health, not its measurement.
    if (t.type == "battery" || t.type == "rssi") {
        DeviceEntry& health = m_devices[t.deviceId];
        health.id = t.deviceId;
        health.zone = t.zone;
        if (t.type == "battery") health.battery = t.value;
        else health.rssi = t.value;
        refreshDeviceRow(health);
        return;
    }

    DeviceEntry& d = m_devices[t.deviceId];
    d.id = t.deviceId;
    d.zone = t.zone;
    d.type = t.type;
    d.unit = t.unit;
    d.value = t.value;
    d.online = true;
    d.updated = QDateTime::currentDateTimeUtc();
    refreshDeviceRow(d);

    ZoneEntry& z = m_zones[t.zone];
    const double x = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    if (t.type == "temperature") {
        z.temperature = t.value;
        appendPoint(z.tempHistory, x, t.value);
    } else if (t.type == "humidity") {
        z.humidity = t.value;
        appendPoint(z.humHistory, x, t.value);
    } else if (t.type == "co2") {
        z.co2 = t.value;
        appendPoint(z.co2History, x, t.value);
    }
    refreshOverview();
}

void MainWindow::onStatus(const DeviceStatus& s) {
    ensureZone(s.zone);

    DeviceEntry& d = m_devices[s.deviceId];
    d.id = s.deviceId;
    d.zone = s.zone;
    d.type = s.type;
    d.firmware = s.firmware;
    d.online = s.online;
    d.updated = QDateTime::currentDateTimeUtc();
    refreshDeviceRow(d);

    if (m_selectedDevice == s.deviceId) updateControls();
    refreshOverview();
}

void MainWindow::onState(const DeviceState& s) {
    ensureZone(s.zone);

    DeviceEntry& d = m_devices[s.deviceId];
    d.id = s.deviceId;
    d.zone = s.zone;
    d.type = s.type;
    d.powered = s.powered;
    d.setpoint = s.setpoint;
    d.updated = QDateTime::currentDateTimeUtc();

    ZoneEntry& z = m_zones[s.zone];
    if (s.type == "hvac") {
        z.hvacOn = s.powered;
        z.setpoint = s.setpoint;
    } else if (s.type == "fan") {
        z.fanOn = s.powered;
    } else if (s.type == "light") {
        z.lightOn = s.powered;
    }

    refreshDeviceRow(d);
    if (m_selectedDevice == s.deviceId) updateControls();
    refreshOverview();
}

void MainWindow::onAlert(const Alert& a) {
    // Alerts arrive with their database id, so a repeat (for example from history) is updated, not added twice.
    const quint64 id = static_cast<quint64>(a.id);
    AlertEntry* known = nullptr;
    if (id != 0) {
        for (AlertEntry& e : m_alerts) {
            if (e.id == id) { known = &e; break; }
        }
    }

    if (known) {
        known->acknowledged = a.acknowledged;
        known->ackBy = a.ackBy;
        known->ackAt = a.ackAt;
    } else {
        AlertEntry e;
        e.id = id;
        e.time = a.timestamp;
        e.severity = a.severity;
        e.zone = a.zone;
        e.deviceId = a.deviceId;
        e.message = a.message;
        e.acknowledged = a.acknowledged;
        e.ackBy = a.ackBy;
        e.ackAt = a.ackAt;
        m_alerts.append(e);
        if (a.severity == Severity::Critical)
            statusBar()->showMessage(QStringLiteral("CRITICAL: %1").arg(a.message), 8000);
        notifyCritical(a.message);
    }

    while (m_alerts.size() > kMaxAlerts) m_alerts.removeFirst();
    rebuildAlertTable();
    refreshOverview();
}

void MainWindow::onAlertAcknowledged(const AlertAck& ack) {
    // Broadcast by the server once it has accepted an acknowledgement, so every dashboard agrees.
    const quint64 id = static_cast<quint64>(ack.id);
    for (AlertEntry& a : m_alerts) {
        if (a.id == id) {
            a.acknowledged = true;
            a.ackBy = ack.by;
            a.ackAt = ack.at;
            break;
        }
    }
    rebuildAlertTable();
    refreshOverview();
}

void MainWindow::onHistoryReceived(const HistoryReply& reply) {
    // Trends: the server's history first, then any live points that arrived after it.
    QHash<QString, QList<QPointF>> series;   // key: zone|type
    for (const Telemetry& t : reply.telemetry) {
        ensureZone(t.zone);
        if (t.type != "temperature" && t.type != "humidity" && t.type != "co2") continue;
        const QString key = t.zone + QLatin1Char('|') + t.type;
        series[key].append(QPointF(static_cast<double>(t.timestamp.toMSecsSinceEpoch()), t.value));
    }
    for (auto it = series.begin(); it != series.end(); ++it) {
        const int bar = it.key().indexOf(QLatin1Char('|'));
        const QString zone = it.key().left(bar);
        const QString type = it.key().mid(bar + 1);

        QList<QPointF> merged = it.value();
        std::sort(merged.begin(), merged.end(),
                  [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });

        ZoneEntry& z = m_zones[zone];
        QList<QPointF>& live = historyFor(z, type);
        const double lastX = merged.isEmpty() ? -1.0 : merged.last().x();
        for (const QPointF& p : std::as_const(live)) {
            if (p.x() > lastX) merged.append(p);
        }
        while (merged.size() > kMaxTrendPoints) merged.removeFirst();
        live = merged;
    }

    // Current values, but only where live data has not already filled them in.
    // The reply is oldest first, so walk it backwards to pick up the newest value.
    for (qsizetype i = reply.telemetry.size(); i-- > 0;) {
        const Telemetry& t = reply.telemetry.at(i);
        ZoneEntry& z = m_zones[t.zone];
        if (t.type == "temperature" && std::isnan(z.temperature)) z.temperature = t.value;
        else if (t.type == "humidity" && std::isnan(z.humidity)) z.humidity = t.value;
        else if (t.type == "co2" && std::isnan(z.co2)) z.co2 = t.value;

        DeviceEntry& d = m_devices[t.deviceId];
        if (t.type == "battery") {
            if (std::isnan(d.battery)) { d.battery = t.value; refreshDeviceRow(d); }
            continue;
        }
        if (t.type == "rssi") {
            if (std::isnan(d.rssi)) { d.rssi = t.value; refreshDeviceRow(d); }
            continue;
        }
        if (!std::isnan(d.value)) continue;
        d.id = t.deviceId;
        d.zone = t.zone;
        d.type = t.type;
        d.unit = t.unit;
        d.value = t.value;
        d.updated = t.timestamp;
        refreshDeviceRow(d);
    }

    // Alerts: add the ones we have not seen, refresh the acknowledgement state of the rest.
    for (const Alert& a : reply.alerts) {
        const quint64 id = static_cast<quint64>(a.id);
        bool known = false;
        for (AlertEntry& e : m_alerts) {
            if (e.id == id) {
                e.acknowledged = a.acknowledged;
                e.ackBy = a.ackBy;
                e.ackAt = a.ackAt;
                known = true;
                break;
            }
        }
        if (known) continue;

        AlertEntry e;
        e.id = id;
        e.time = a.timestamp;
        e.severity = a.severity;
        e.zone = a.zone;
        e.deviceId = a.deviceId;
        e.message = a.message;
        e.acknowledged = a.acknowledged;
        e.ackBy = a.ackBy;
        e.ackAt = a.ackAt;
        m_alerts.append(e);
    }
    std::sort(m_alerts.begin(), m_alerts.end(),
              [](const AlertEntry& x, const AlertEntry& y) { return x.id < y.id; });
    while (m_alerts.size() > kMaxAlerts) m_alerts.removeFirst();

    rebuildAlertTable();
    refreshOverview();
    statusBar()->showMessage(QStringLiteral("History loaded: %1 points, %2 alerts")
                                 .arg(static_cast<int>(reply.telemetry.size()))
                                 .arg(static_cast<int>(reply.alerts.size())), 5000);
}

// ---------------------------------------------------------------- floor plan, settings, theme

QWidget* MainWindow::buildFloorPlanTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Floor plan"));
    title->setObjectName("sectionTitle");
    lay->addWidget(title);

    m_floorPlan = new FloorPlanWidget;
    m_floorPlan->setOnSelect([this](const QString& zone) {
        setZoneFilter(zone);
        m_tabs->setCurrentIndex(0);
    });
    lay->addWidget(m_floorPlan, 1);

    auto* note = new QLabel(QStringLiteral(
        "Click a room to open its trends. The colour shows its status. Layout file: config/floorplan.json"));
    note->setObjectName("muted");
    lay->addWidget(note);

    auto rooms = sb::loadFloorPlanFile(QStringLiteral("config/floorplan.json"));
    if (rooms) m_floorRooms = *rooms;
    else qWarning() << "Floor plan not loaded:" << rooms.error();
    return page;
}

QWidget* MainWindow::buildSettingsTab() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(20, 18, 20, 18);

    m_thresholdEditor = new ThresholdEditor([this](const sb::ThresholdConfig& cfg) {
        m_mqtt.sendThresholds(cfg);
        statusBar()->showMessage(QStringLiteral("Thresholds sent to the server"), 4000);
    });
    m_thresholdEditor->setConfig(m_config);
    lay->addWidget(m_thresholdEditor);
    lay->addStretch(1);
    return page;
}

void MainWindow::applyTheme() {
    m_palette = m_lightTheme ? lightPalette() : darkPalette();
    setStyleSheet(buildStyleSheet(m_palette));
    m_tempTrend->setColors(m_palette);
    m_humTrend->setColors(m_palette);
    m_co2Trend->setColors(m_palette);
    m_themeButton->setText(m_lightTheme ? QStringLiteral("Dark theme") : QStringLiteral("Light theme"));
}

void MainWindow::onToggleTheme() {
    m_lightTheme = !m_lightTheme;
    QSettings(QStringLiteral("SmartBuilding"), QStringLiteral("Monitor"))
        .setValue(QStringLiteral("theme"), m_lightTheme ? QStringLiteral("light") : QStringLiteral("dark"));
    applyTheme();
}

void MainWindow::onToggleSound() {
    m_soundOn = !m_soundOn;
    m_soundButton->setText(m_soundOn ? QStringLiteral("Sound: on") : QStringLiteral("Sound: muted"));
}

void MainWindow::notifyCritical(const QString& message) {
    if (m_soundOn) QApplication::beep();
    // A tray balloon only when the dashboard is not in front, so it does not repeat what is on screen.
    if (m_tray && !isActiveWindow())
        m_tray->showMessage(QStringLiteral("Critical alert"), message, QSystemTrayIcon::Warning, 8000);
}

void MainWindow::onThresholdsReceived(const sb::ThresholdConfig& cfg) {
    m_config = cfg;
    if (m_thresholdEditor) m_thresholdEditor->setConfig(cfg);
    updateTrendLimits();
    refreshOverview();
    statusBar()->showMessage(QStringLiteral("Thresholds updated from the server"), 4000);
}

void MainWindow::updateTrendLimits() {
    const double tempHigh = m_config.limitOf(QStringLiteral("temperature-high"), kTempHigh);
    const double humHigh = m_config.limitOf(QStringLiteral("humidity-high"), kHumHigh);
    const double co2High = m_config.limitOf(QStringLiteral("co2-high"), kCo2High);
    m_tempTrend->setThreshold(tempHigh, QStringLiteral("limit %1 °C").arg(QString::number(tempHigh, 'f', 1)));
    m_humTrend->setThreshold(humHigh, QStringLiteral("limit %1 %RH").arg(QString::number(humHigh, 'f', 0)));
    m_co2Trend->setThreshold(co2High, QStringLiteral("limit %1 ppm").arg(QString::number(co2High, 'f', 0)));
}

void MainWindow::updateFloorPlan() {
    if (!m_floorPlan) return;
    QList<FloorRoomView> views;
    for (const sb::FloorRoom& room : std::as_const(m_floorRooms)) {
        FloorRoomView v;
        v.zone = room.zone;
        v.label = room.label;
        v.rect = QRectF(room.x, room.y, room.w, room.h);
        v.level = zoneLevel(room.zone);
        v.selected = room.zone == m_zoneFilter;

        const ZoneEntry z = m_zones.value(room.zone);
        v.lines << (std::isnan(z.temperature) ? QStringLiteral("temperature --")
                                              : QStringLiteral("%1 °C").arg(QString::number(z.temperature, 'f', 1)));
        v.lines << (std::isnan(z.humidity) ? QStringLiteral("humidity --")
                                           : QStringLiteral("%1 %RH").arg(QString::number(z.humidity, 'f', 0)));
        v.lines << (std::isnan(z.co2) ? QStringLiteral("CO2 --")
                                      : QStringLiteral("CO2 %1 ppm").arg(QString::number(z.co2, 'f', 0)));
        const int alerts = activeAlerts(room.zone);
        if (alerts > 0) v.lines << QStringLiteral("%1 active alert(s)").arg(alerts);
        views.append(v);
    }
    m_floorPlan->setRooms(views);
}

// ---------------------------------------------------------------- exports and reports

void MainWindow::onExportTrends() {
    if (m_zoneFilter.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a zone first (zone card or floor plan)"), 4000);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export trends"),
                                                      QStringLiteral("%1-trends.csv").arg(m_zoneFilter),
                                                      QStringLiteral("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    const ZoneEntry z = m_zones.value(m_zoneFilter);
    QList<QStringList> rows;
    auto addSeries = [&rows](const QList<QPointF>& series, const QString& name, const QString& unit) {
        for (const QPointF& p : series) {
            rows.append(QStringList{QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(p.x())).toString(Qt::ISODate),
                                    name, QString::number(p.y(), 'f', 2), unit});
        }
    };
    addSeries(z.tempHistory, QStringLiteral("temperature"), QStringLiteral("°C"));
    addSeries(z.humHistory, QStringLiteral("humidity"), QStringLiteral("%RH"));
    addSeries(z.co2History, QStringLiteral("co2"), QStringLiteral("ppm"));

    const QStringList header{QStringLiteral("time"), QStringLiteral("measurement"),
                             QStringLiteral("value"), QStringLiteral("unit")};
    if (reports::writeFile(path, reports::toCsv(header, rows)))
        statusBar()->showMessage(QStringLiteral("Exported %1 rows").arg(static_cast<int>(rows.size())), 4000);
    else
        statusBar()->showMessage(QStringLiteral("Could not write %1").arg(path), 6000);
}

void MainWindow::onExportAlerts() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export alerts"),
                                                      QStringLiteral("smartbuilding-alerts.csv"),
                                                      QStringLiteral("CSV files (*.csv)"));
    if (path.isEmpty()) return;

    QList<QStringList> rows;
    for (const AlertEntry& a : std::as_const(m_alerts)) {
        if (!alertMatchesFilters(a)) continue;
        QString state;
        if (a.acknowledged) state = QStringLiteral("Acknowledged");
        else if (a.severity == Severity::Info) state = QStringLiteral("Info");
        else state = QStringLiteral("Active");
        rows.append(QStringList{a.time.toLocalTime().toString(Qt::ISODate), severityLabel(a.severity),
                                a.zone, a.deviceId, a.message, state, a.ackBy});
    }
    const QStringList header{QStringLiteral("time"), QStringLiteral("severity"), QStringLiteral("zone"),
                             QStringLiteral("device"), QStringLiteral("message"), QStringLiteral("state"),
                             QStringLiteral("acknowledged_by")};
    if (reports::writeFile(path, reports::toCsv(header, rows)))
        statusBar()->showMessage(QStringLiteral("Exported %1 alerts").arg(static_cast<int>(rows.size())), 4000);
    else
        statusBar()->showMessage(QStringLiteral("Could not write %1").arg(path), 6000);
}

void MainWindow::onExportReport() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save report"),
        QStringLiteral("smartbuilding-report-%1.html").arg(QDate::currentDate().toString(Qt::ISODate)),
        QStringLiteral("HTML files (*.html)"));
    if (path.isEmpty()) return;

    // The report covers whatever range is loaded: choose "Last 24 hours" for a daily report,
    // "Last 30 days" or custom dates for a monthly one.
    qint64 first = 0;
    qint64 last = 0;
    bool any = false;
    auto span = [&](const QList<QPointF>& series) {
        for (const QPointF& p : series) {
            const qint64 t = static_cast<qint64>(p.x());
            if (!any) { first = t; last = t; any = true; }
            first = std::min(first, t);
            last = std::max(last, t);
        }
    };
    for (const ZoneEntry& z : std::as_const(m_zones)) {
        span(z.tempHistory);
        span(z.humHistory);
        span(z.co2History);
    }

    auto stat = [](const QList<QPointF>& series, bool wantMax, const QString& unit, int decimals) {
        if (series.isEmpty()) return QStringLiteral("--");
        double v = series.first().y();
        for (const QPointF& p : series) v = wantMax ? std::max(v, p.y()) : std::min(v, p.y());
        return QStringLiteral("%1 %2").arg(QString::number(v, 'f', decimals), unit);
    };

    const QStringList headers{QStringLiteral("Zone"), QStringLiteral("Peak temperature"),
                              QStringLiteral("Lowest humidity"), QStringLiteral("Highest humidity"),
                              QStringLiteral("Peak CO2"), QStringLiteral("Critical alerts"),
                              QStringLiteral("Warnings"), QStringLiteral("Acknowledged")};
    QList<QStringList> rows;
    int critical = 0;
    int warnings = 0;
    int infos = 0;
    for (const QString& zone : std::as_const(m_zoneOrder)) {
        const ZoneEntry z = m_zones.value(zone);
        int zoneCritical = 0;
        int zoneWarnings = 0;
        int zoneAcked = 0;
        for (const AlertEntry& a : std::as_const(m_alerts)) {
            if (a.zone != zone) continue;
            if (a.acknowledged) ++zoneAcked;
            if (a.severity == Severity::Critical) ++zoneCritical;
            else if (a.severity == Severity::Warning) ++zoneWarnings;
        }
        critical += zoneCritical;
        warnings += zoneWarnings;
        rows.append(QStringList{zone,
                                stat(z.tempHistory, true, QStringLiteral("°C"), 1),
                                stat(z.humHistory, false, QStringLiteral("%RH"), 0),
                                stat(z.humHistory, true, QStringLiteral("%RH"), 0),
                                stat(z.co2History, true, QStringLiteral("ppm"), 0),
                                QString::number(zoneCritical), QString::number(zoneWarnings),
                                QString::number(zoneAcked)});
    }
    for (const AlertEntry& a : std::as_const(m_alerts)) {
        if (a.severity == Severity::Info) ++infos;
    }

    QStringList notes;
    if (any) {
        notes << QStringLiteral("Data covered: %1 to %2")
                     .arg(QDateTime::fromMSecsSinceEpoch(first).toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                          QDateTime::fromMSecsSinceEpoch(last).toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    } else {
        notes << QStringLiteral("No measurements loaded. Choose a range in the toolbar first.");
    }
    notes << QStringLiteral("Alerts: %1 critical, %2 warnings, %3 informational.")
                 .arg(critical).arg(warnings).arg(infos);
    notes << QStringLiteral("Generated %1 by %2")
                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), operatorName());

    if (reports::writeFile(path, reports::toHtml(QStringLiteral("SmartBuilding report"), notes, headers, rows)))
        statusBar()->showMessage(QStringLiteral("Report saved"), 4000);
    else
        statusBar()->showMessage(QStringLiteral("Could not write %1").arg(path), 6000);
}

// ---------------------------------------------------------------- history range

void MainWindow::onRangeChanged(int index) {
    const bool custom = index == kCustomRangeIndex;
    m_fromDate->setVisible(custom);
    m_toDate->setVisible(custom);
    m_applyRange->setVisible(custom);
    if (!custom) requestSelectedRange();
}

void MainWindow::requestSelectedRange() {
    if (!m_mqtt.isConnected()) return;   // requested again when the connection comes up

    const int index = m_rangeCombo->currentIndex();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (index >= 0 && index < kCustomRangeIndex) {
        requestHistoryRange(now - kRangeMs[index], 0);
        return;
    }

    // Custom dates: whole local days, from the first day to the end of the last day.
    qint64 since = QDateTime(m_fromDate->date(), QTime(0, 0)).toMSecsSinceEpoch();
    qint64 until = QDateTime(m_toDate->date(), QTime(23, 59, 59, 999)).toMSecsSinceEpoch();
    if (until < since) std::swap(since, until);
    requestHistoryRange(since, until);
}

void MainWindow::requestHistoryRange(qint64 sinceMs, qint64 untilMs) {
    // Replace what is shown: the server's answer covers the whole requested window.
    for (ZoneEntry& z : m_zones) {
        z.tempHistory.clear();
        z.humHistory.clear();
        z.co2History.clear();
    }
    m_alerts.clear();
    rebuildAlertTable();
    refreshOverview();
    m_mqtt.requestHistory(sinceMs, QString(), untilMs);
}

// ---------------------------------------------------------------- device filter

void MainWindow::onDeviceTypeFilterChanged(int) {
    applyDeviceFilter();
}

bool MainWindow::deviceVisible(const DeviceEntry& d) const {
    if (!m_zoneFilter.isEmpty() && d.zone != m_zoneFilter) return false;
    const QString type = m_deviceTypeFilter->currentData().toString();
    return type.isEmpty() || d.type == type;
}

// ---------------------------------------------------------------- filters

void MainWindow::setZoneFilter(const QString& zone) {
    const int index = zone.isEmpty() ? 0 : m_zoneCombo->findText(zone);
    if (index >= 0) m_zoneCombo->setCurrentIndex(index);
}

void MainWindow::onZoneFilterChanged(int index) {
    m_zoneFilter = index <= 0 ? QString() : m_zoneCombo->itemText(index);
    applyDeviceFilter();
    rebuildAlertTable();
    refreshOverview();
}

void MainWindow::onAlertFilterChanged() {
    rebuildAlertTable();
}

bool MainWindow::alertMatchesFilters(const AlertEntry& a) const {
    if (!m_zoneFilter.isEmpty() && a.zone != m_zoneFilter) return false;
    switch (m_alertSeverity->currentIndex()) {
    case 1: return a.severity == Severity::Critical;
    case 2: return a.severity == Severity::Warning;
    case 3: return a.severity == Severity::Info;
    default: return true;
    }
}

// ---------------------------------------------------------------- devices tab

void MainWindow::refreshDeviceRow(const DeviceEntry& d) {
    int row = m_rowByDevice.value(d.id, -1);
    if (row < 0) {
        row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        m_rowByDevice.insert(d.id, row);
        for (int c = 0; c < m_deviceTable->columnCount(); ++c)
            m_deviceTable->setItem(row, c, new QTableWidgetItem);
    }

    m_deviceTable->item(row, 0)->setText(d.id);
    m_deviceTable->item(row, 1)->setText(d.type);
    m_deviceTable->item(row, 2)->setText(d.zone);

    QTableWidgetItem* status = m_deviceTable->item(row, 3);
    status->setText(d.online ? QStringLiteral("Online") : QStringLiteral("Offline"));
    status->setForeground(d.online ? QColor("#22c55e") : QColor("#ef4444"));

    m_deviceTable->item(row, 4)->setText(d.firmware);
    m_deviceTable->item(row, 5)->setText(describeValue(d));
    QTableWidgetItem* battery = m_deviceTable->item(row, 6);
    if (std::isnan(d.battery)) {
        battery->setText(QStringLiteral("--"));
        battery->setForeground(QColor("#8a9bb8"));
    } else {
        battery->setText(QStringLiteral("%1%").arg(static_cast<int>(std::round(d.battery))));
        battery->setForeground(d.battery > 50.0 ? QColor("#22c55e")
                               : d.battery > 20.0 ? QColor("#f59e0b") : QColor("#ef4444"));
    }

    QTableWidgetItem* signal = m_deviceTable->item(row, 7);
    if (std::isnan(d.rssi)) {
        signal->setText(QStringLiteral("--"));
        signal->setForeground(QColor("#8a9bb8"));
    } else {
        signal->setText(QStringLiteral("%1 dBm").arg(static_cast<int>(std::round(d.rssi))));
        signal->setForeground(d.rssi > -70.0 ? QColor("#22c55e")
                              : d.rssi > -85.0 ? QColor("#f59e0b") : QColor("#ef4444"));
    }

    m_deviceTable->item(row, 8)->setText(d.updated.toLocalTime().toString(QStringLiteral("HH:mm:ss")));

    m_deviceTable->setRowHidden(row, !deviceVisible(d));
}

void MainWindow::applyDeviceFilter() {
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        const int row = m_rowByDevice.value(it.key(), -1);
        if (row >= 0) m_deviceTable->setRowHidden(row, !deviceVisible(it.value()));
    }
}

void MainWindow::onDeviceSelected() {
    const int row = m_deviceTable->currentRow();
    if (row < 0 || !m_deviceTable->item(row, 0)) {
        m_selectedDevice.clear();
    } else {
        m_selectedDevice = m_deviceTable->item(row, 0)->text();
        const DeviceEntry d = m_devices.value(m_selectedDevice);
        if (d.type == "hvac") m_setpoint->setValue(d.setpoint > 0.0 ? d.setpoint : 22.0);
    }
    updateControls();
}

void MainWindow::updateControls() {
    const bool hasDevice = m_devices.contains(m_selectedDevice);
    const DeviceEntry d = m_devices.value(m_selectedDevice);
    const bool actuator = hasDevice && (d.type == "hvac" || d.type == "light" || d.type == "fan");
    const bool hvac = hasDevice && d.type == "hvac";

    m_powerButton->setEnabled(actuator);
    m_powerButton->setText(actuator && d.powered ? QStringLiteral("Turn off") : QStringLiteral("Turn on"));
    m_setpoint->setEnabled(hvac);
    m_applyButton->setEnabled(hvac);

    if (hasDevice) {
        const QString state = actuator ? onOff(d.powered) : QStringLiteral("sensor");
        m_controlTitle->setText(d.id);
        m_controlDetail->setText(QStringLiteral("%1  ·  %2  ·  %3").arg(d.zone, d.type, state));
    } else {
        m_controlTitle->setText(QStringLiteral("No device selected"));
        m_controlDetail->setText(QStringLiteral("Select a device in the table to control an HVAC unit, light or fan."));
    }
}

void MainWindow::onTogglePower() {
    if (m_selectedDevice.isEmpty()) return;
    const DeviceEntry d = m_devices.value(m_selectedDevice);
    m_mqtt.sendCommand(d.zone, d.id, Command{QStringLiteral("power"), d.powered ? 0.0 : 1.0});
    statusBar()->showMessage(QStringLiteral("Power command sent to %1").arg(d.id), 3000);
}

void MainWindow::onApplySetpoint() {
    if (m_selectedDevice.isEmpty()) return;
    const DeviceEntry d = m_devices.value(m_selectedDevice);
    const double sp = m_setpoint->value();
    m_mqtt.sendCommand(d.zone, d.id, Command{QStringLiteral("setpoint"), sp});
    statusBar()->showMessage(QStringLiteral("Setpoint %1 °C sent to %2").arg(QString::number(sp, 'f', 1), d.id), 3000);
}

// ---------------------------------------------------------------- alerts tab

void MainWindow::rebuildAlertTable() {
    m_alertTable->setRowCount(0);
    for (int i = m_alerts.size() - 1; i >= 0; --i) {   // newest first
        const AlertEntry& a = m_alerts.at(i);
        if (!alertMatchesFilters(a)) continue;

        const int row = m_alertTable->rowCount();
        m_alertTable->insertRow(row);

        const QColor color = a.acknowledged ? QColor("#8a9bb8") : severityColor(a.severity);
        QString state;
        if (a.acknowledged) state = QStringLiteral("Acknowledged by %1").arg(a.ackBy);
        else if (a.severity == Severity::Info) state = QStringLiteral("Info");
        else state = QStringLiteral("Active");

        auto* timeItem = makeItem(a.time.toLocalTime().toString(QStringLiteral("HH:mm:ss")), color);
        timeItem->setData(Qt::UserRole, QVariant::fromValue(a.id));
        m_alertTable->setItem(row, 0, timeItem);
        m_alertTable->setItem(row, 1, makeItem(severityLabel(a.severity), color));
        m_alertTable->setItem(row, 2, makeItem(a.zone, color));
        m_alertTable->setItem(row, 3, makeItem(a.deviceId, color));
        m_alertTable->setItem(row, 4, makeItem(a.message, color));
        m_alertTable->setItem(row, 5, makeItem(state, color));
    }
}

void MainWindow::markAcknowledged(const QList<quint64>& ids) {
    // Send each request to the server. The table changes when the server's broadcast arrives.
    int sent = 0;
    for (const AlertEntry& a : std::as_const(m_alerts)) {
        if (ids.contains(a.id) && !a.acknowledged && a.severity != Severity::Info) {
            m_mqtt.sendAck(static_cast<qint64>(a.id), operatorName());
            ++sent;
        }
    }
    statusBar()->showMessage(QStringLiteral("Sent %1 acknowledgement(s)").arg(sent), 3000);
}

void MainWindow::onAcknowledgeSelected() {
    QList<quint64> ids;
    const QModelIndexList rows = m_alertTable->selectionModel()->selectedRows();
    for (const QModelIndex& index : rows) {
        if (QTableWidgetItem* item = m_alertTable->item(index.row(), 0))
            ids.append(item->data(Qt::UserRole).toULongLong());
    }
    markAcknowledged(ids);
}

void MainWindow::onAcknowledgeAll() {
    QList<quint64> ids;
    for (const AlertEntry& a : std::as_const(m_alerts)) {
        if (alertMatchesFilters(a)) ids.append(a.id);
    }
    markAcknowledged(ids);
}

// ---------------------------------------------------------------- overview

int MainWindow::activeAlerts(const QString& zone, bool criticalOnly) const {
    int count = 0;
    for (const AlertEntry& a : m_alerts) {
        if (a.acknowledged || a.severity == Severity::Info) continue;
        if (!zone.isEmpty() && a.zone != zone) continue;
        if (criticalOnly && a.severity != Severity::Critical) continue;
        ++count;
    }
    return count;
}

QString MainWindow::zoneLevel(const QString& zone) const {
    if (activeAlerts(zone, true) > 0) return QStringLiteral("critical");
    if (activeAlerts(zone) > 0) return QStringLiteral("warning");

    bool hasData = false;
    for (auto it = m_zones.cbegin(); it != m_zones.cend(); ++it) {
        if (!zone.isEmpty() && it.key() != zone) continue;
        const ZoneEntry& z = it.value();
        if (!std::isnan(z.temperature) || !std::isnan(z.humidity) || !std::isnan(z.co2)) hasData = true;
    }
    return hasData ? QStringLiteral("ok") : QStringLiteral("nodata");
}

void MainWindow::updateZoneCard(const QString& zone) {
    ZoneCard* card = m_zoneCards.value(zone, nullptr);
    if (!card) return;
    const bool selected = (zone == m_zoneFilter);
    const QString level = zoneLevel(zone);
    const int alerts = activeAlerts(zone);

    QString statusText;
    if (level == "critical")      statusText = QStringLiteral("CRITICAL  %1").arg(alerts);
    else if (level == "warning")  statusText = QStringLiteral("WARNING  %1").arg(alerts);
    else if (level == "ok")       statusText = QStringLiteral("NORMAL");
    else                          statusText = QStringLiteral("NO DATA");

    if (zone.isEmpty()) {
        // Building aggregate
        double tSum = 0.0, hSum = 0.0, co2Peak = std::nan("");
        int tN = 0, hN = 0, hvacOn = 0, fanOn = 0, lightOn = 0;
        for (const ZoneEntry& z : std::as_const(m_zones)) {
            if (!std::isnan(z.temperature)) { tSum += z.temperature; ++tN; }
            if (!std::isnan(z.humidity)) { hSum += z.humidity; ++hN; }
            if (!std::isnan(z.co2) && (std::isnan(co2Peak) || z.co2 > co2Peak)) co2Peak = z.co2;
            hvacOn += z.hvacOn ? 1 : 0;
            fanOn += z.fanOn ? 1 : 0;
            lightOn += z.lightOn ? 1 : 0;
        }
        const int zones = static_cast<int>(m_zones.size());
        const QString temp = tN ? QStringLiteral("%1 °C").arg(QString::number(tSum / tN, 'f', 1))
                                : QStringLiteral("--");
        const QString metrics = QStringLiteral("Humidity avg %1   CO2 peak %2")
            .arg(hN ? QStringLiteral("%1 %RH").arg(QString::number(hSum / hN, 'f', 0)) : QStringLiteral("--"),
                 std::isnan(co2Peak) ? QStringLiteral("--") : QStringLiteral("%1 ppm").arg(QString::number(co2Peak, 'f', 0)));
        const QString equipment = QStringLiteral("%1 of %2 zones  ·  HVAC on %3  ·  Fans on %4  ·  Lights on %5")
            .arg(zones).arg(zones).arg(hvacOn).arg(fanOn).arg(lightOn);
        card->setContent(temp, metrics, equipment, level, statusText, selected);
        return;
    }

    const ZoneEntry z = m_zones.value(zone);
    const QString temp = std::isnan(z.temperature) ? QStringLiteral("--")
                                                   : QStringLiteral("%1 °C").arg(QString::number(z.temperature, 'f', 1));
    const QString hum = std::isnan(z.humidity) ? QStringLiteral("--") : QString::number(z.humidity, 'f', 0) + QStringLiteral(" %RH");
    const QString co2 = std::isnan(z.co2) ? QStringLiteral("--") : QString::number(z.co2, 'f', 0) + QStringLiteral(" ppm");
    const QString metrics = QStringLiteral("Humidity %1   CO2 %2").arg(hum, co2);

    QString equipment = QStringLiteral("HVAC %1  ·  Fan %2  ·  Light %3")
        .arg(onOff(z.hvacOn), onOff(z.fanOn), onOff(z.lightOn));
    if (!std::isnan(z.setpoint))
        equipment += QStringLiteral("  ·  setpoint %1 °C").arg(QString::number(z.setpoint, 'f', 1));

    card->setContent(temp, metrics, equipment, level, statusText, selected);
}

void MainWindow::updateTrends() {
    if (m_zoneFilter.isEmpty()) {
        m_trendTitle->setText(QStringLiteral("Trends: select a zone to view its history"));
        m_tempTrend->setPoints({});
        m_humTrend->setPoints({});
        m_co2Trend->setPoints({});
        return;
    }
    const ZoneEntry z = m_zones.value(m_zoneFilter);
    m_trendTitle->setText(QStringLiteral("Trends: %1").arg(m_zoneFilter));
    m_tempTrend->setPoints(z.tempHistory);
    m_humTrend->setPoints(z.humHistory);
    m_co2Trend->setPoints(z.co2History);
}

void MainWindow::refreshOverview() {
    const double tempHigh = m_config.limitOf(QStringLiteral("temperature-high"), kTempHigh);
    const double humHigh = m_config.limitOf(QStringLiteral("humidity-high"), kHumHigh);
    const double co2High = m_config.limitOf(QStringLiteral("co2-high"), kCo2High);
    // KPI tiles
    double tSum = 0.0, hSum = 0.0, co2Peak = std::nan("");
    int tN = 0, hN = 0;
    bool tWarn = false, hWarn = false, cWarn = false;
    for (const ZoneEntry& z : std::as_const(m_zones)) {
        if (!std::isnan(z.temperature)) { tSum += z.temperature; ++tN; if (z.temperature > tempHigh) tWarn = true; }
        if (!std::isnan(z.humidity)) { hSum += z.humidity; ++hN; if (z.humidity > humHigh) hWarn = true; }
        if (!std::isnan(z.co2)) {
            if (std::isnan(co2Peak) || z.co2 > co2Peak) co2Peak = z.co2;
            if (z.co2 > co2High) cWarn = true;
        }
    }

    int online = 0;
    for (const DeviceEntry& d : std::as_const(m_devices)) if (d.online) ++online;
    const int total = static_cast<int>(m_devices.size());
    const int active = activeAlerts(QString());
    const bool anyCritical = activeAlerts(QString(), true) > 0;

    m_kpiTemp->setKpi(tN ? QStringLiteral("%1 °C").arg(QString::number(tSum / tN, 'f', 1)) : QStringLiteral("--"),
                      tN ? (tWarn ? QStringLiteral("warning") : QStringLiteral("ok")) : QStringLiteral("nodata"),
                      QStringLiteral("%1 zones reporting").arg(tN));
    m_kpiHum->setKpi(hN ? QStringLiteral("%1 %RH").arg(QString::number(hSum / hN, 'f', 0)) : QStringLiteral("--"),
                     hN ? (hWarn ? QStringLiteral("warning") : QStringLiteral("ok")) : QStringLiteral("nodata"),
                     QStringLiteral("%1 zones reporting").arg(hN));
    m_kpiCo2->setKpi(std::isnan(co2Peak) ? QStringLiteral("--") : QStringLiteral("%1 ppm").arg(QString::number(co2Peak, 'f', 0)),
                     std::isnan(co2Peak) ? QStringLiteral("nodata") : (cWarn ? QStringLiteral("warning") : QStringLiteral("ok")),
                     QStringLiteral("highest zone reading"));
    m_kpiDevices->setKpi(QStringLiteral("%1 / %2").arg(online).arg(total),
                         total == 0 ? QStringLiteral("nodata") : (online < total ? QStringLiteral("warning") : QStringLiteral("ok")),
                         QStringLiteral("devices reporting"));
    m_kpiAlerts->setKpi(QString::number(active),
                        anyCritical ? QStringLiteral("critical") : (active > 0 ? QStringLiteral("warning") : QStringLiteral("ok")),
                        anyCritical ? QStringLiteral("critical alert active") : QStringLiteral("unacknowledged warnings"));

    // Zone cards
    updateZoneCard(QString());
    for (const QString& zone : std::as_const(m_zoneOrder)) updateZoneCard(zone);

    updateTrends();
    updateFloorPlan();

    m_tabs->setTabText(m_tabs->indexOf(m_devicesPage), QStringLiteral("Devices (%1)").arg(total));
    m_tabs->setTabText(m_tabs->indexOf(m_alertsPage), QStringLiteral("Alerts (%1 active)").arg(active));
    m_statusLabel->setText(QStringLiteral("Broker %1   ·   %2 of %3 devices online   ·   %4 zones")
                               .arg(m_brokerText).arg(online).arg(total).arg(static_cast<int>(m_zones.size())));
}
