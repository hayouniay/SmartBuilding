#pragma once

// Settings panel for alert limits. Apply hands a validated copy to the callback,
// which sends it to the server. The server stores it and publishes it to every dashboard.

#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QVBoxLayout>
#include <functional>
#include <utility>

#include "common/ThresholdConfig.h"

class ThresholdEditor : public QFrame {
public:
    using ApplyFn = std::function<void(const sb::ThresholdConfig&)>;

    explicit ThresholdEditor(ApplyFn onApply, QWidget* parent = nullptr)
        : QFrame(parent), m_onApply(std::move(onApply)) {
        setObjectName("panel");
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(18, 16, 18, 16);
        lay->setSpacing(10);

        auto* title = new QLabel(QStringLiteral("Alert thresholds"));
        title->setObjectName("sectionTitle");
        auto* hint = new QLabel(QStringLiteral(
            "The server keeps these limits and sends them to every dashboard, so all operators see the same alerts. "
            "Hysteresis stops an alert from flickering when a value hovers near its limit."));
        hint->setObjectName("muted");
        hint->setWordWrap(true);

        m_grid = new QGridLayout;
        m_grid->setHorizontalSpacing(14);
        m_grid->setVerticalSpacing(8);

        auto* buttons = new QHBoxLayout;
        m_status = new QLabel;
        m_status->setObjectName("muted");
        auto* apply = new QPushButton(QStringLiteral("Apply to all dashboards"));
        QObject::connect(apply, &QPushButton::clicked, [this] {
            const sb::ThresholdConfig cfg = config();
            if (auto ok = sb::validateThresholdConfig(cfg); !ok) {
                m_status->setText(ok.error());
                return;
            }
            m_status->setText(QStringLiteral("Sent. Waiting for the server to confirm..."));
            if (m_onApply) m_onApply(cfg);
        });
        buttons->addWidget(m_status, 1);
        buttons->addWidget(apply);

        lay->addWidget(title);
        lay->addWidget(hint);
        lay->addLayout(m_grid);
        lay->addStretch(1);
        lay->addLayout(buttons);
    }

    // Shows the given settings. Called when the server publishes new ones.
    void setConfig(const sb::ThresholdConfig& cfg) {
        m_cfg = cfg;
        rebuild();
        m_status->setText(QStringLiteral("Showing the settings published by the server."));
    }

    // The settings as currently typed in.
    sb::ThresholdConfig config() const {
        sb::ThresholdConfig out = m_cfg;
        int i = 0;
        for (sb::ThresholdEntry& e : out.thresholds) {
            e.limit = m_limitBoxes.at(i)->value();
            e.hysteresis = m_hystBoxes.at(i)->value();
            ++i;
        }
        for (sb::AutomationEntry& e : out.automation) {
            e.limit = m_limitBoxes.at(i)->value();
            e.hysteresis = m_hystBoxes.at(i)->value();
            ++i;
        }
        return out;
    }

private:
    void rebuild() {
        while (QLayoutItem* item = m_grid->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        m_limitBoxes.clear();
        m_hystBoxes.clear();

        auto* rule = new QLabel(QStringLiteral("Rule"));
        auto* limit = new QLabel(QStringLiteral("Limit"));
        auto* hyst = new QLabel(QStringLiteral("Hysteresis"));
        for (QLabel* h : {rule, limit, hyst}) h->setObjectName("muted");
        m_grid->addWidget(rule, 0, 0);
        m_grid->addWidget(limit, 0, 1);
        m_grid->addWidget(hyst, 0, 2);

        int row = 1;
        for (const sb::ThresholdEntry& e : m_cfg.thresholds) {
            const QString text = QStringLiteral("%1  (%2 %3)").arg(e.name, e.sensorType,
                                                                   e.above ? QStringLiteral("above") : QStringLiteral("below"));
            addRow(row++, text, e.limit, e.hysteresis);
        }
        for (const sb::AutomationEntry& e : m_cfg.automation) {
            const QString text = QStringLiteral("%1  (%2 %3 switches %4)")
                                     .arg(e.name, e.sensorType,
                                          e.above ? QStringLiteral("above") : QStringLiteral("below"), e.targetType);
            addRow(row++, text, e.limit, e.hysteresis);
        }
    }

    void addRow(int row, const QString& text, double limit, double hysteresis) {
        auto* label = new QLabel(text);
        auto* limitBox = new QDoubleSpinBox;
        limitBox->setRange(-100000.0, 100000.0);
        limitBox->setDecimals(1);
        limitBox->setValue(limit);
        auto* hystBox = new QDoubleSpinBox;
        hystBox->setRange(0.0, 100000.0);
        hystBox->setDecimals(1);
        hystBox->setValue(hysteresis);
        m_grid->addWidget(label, row, 0);
        m_grid->addWidget(limitBox, row, 1);
        m_grid->addWidget(hystBox, row, 2);
        m_limitBoxes.append(limitBox);
        m_hystBoxes.append(hystBox);
    }

    ApplyFn m_onApply;
    sb::ThresholdConfig m_cfg;
    QGridLayout* m_grid = nullptr;
    QLabel* m_status = nullptr;
    QList<QDoubleSpinBox*> m_limitBoxes;
    QList<QDoubleSpinBox*> m_hystBoxes;
};
