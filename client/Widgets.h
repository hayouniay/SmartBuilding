#pragma once

// Custom widgets for the monitoring dashboard: KPI tiles, zone status cards, and trend charts.
// Colours and fonts come from the stylesheet in MainWindow.cpp; these widgets only set
// object names and dynamic "level" / "selected" properties.

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QStyle>
#include <QVBoxLayout>
#include <QStringList>
#include <QWidget>

#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

// Qt does not restyle a widget when a dynamic property changes. This forces it.
inline void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// KPI tile: a small title, a large value and one line of context.
// The "level" property ("ok", "warning", "critical", "nodata") colours the value.
class KpiCard : public QFrame {
public:
    explicit KpiCard(const QString& title, QWidget* parent = nullptr) : QFrame(parent) {
        setObjectName("kpi");
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(18, 14, 18, 14);
        lay->setSpacing(2);

        m_title = new QLabel(title.toUpper());
        m_title->setObjectName("kpiTitle");
        m_value = new QLabel(QStringLiteral("--"));
        m_value->setObjectName("kpiValue");
        m_context = new QLabel;
        m_context->setObjectName("muted");

        lay->addWidget(m_title);
        lay->addWidget(m_value);
        lay->addWidget(m_context);
    }

    void setKpi(const QString& value, const QString& level, const QString& context) {
        m_value->setText(value);
        m_value->setProperty("level", level);
        repolish(m_value);
        m_context->setText(context);
    }

private:
    QLabel* m_title = nullptr;
    QLabel* m_value = nullptr;
    QLabel* m_context = nullptr;
};

// Status card for one zone, or for the whole building. A click selects that zone.
class ZoneCard : public QFrame {
public:
    ZoneCard(const QString& title, std::function<void()> onClick, QWidget* parent = nullptr)
        : QFrame(parent), m_onClick(std::move(onClick)) {
        setObjectName("zoneCard");
        setCursor(Qt::PointingHandCursor);
        setMinimumWidth(240);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(16, 14, 16, 14);
        lay->setSpacing(4);

        auto* head = new QHBoxLayout;
        m_name = new QLabel(title);
        m_name->setObjectName("zoneName");
        m_status = new QLabel;
        m_status->setObjectName("zoneStatus");
        head->addWidget(m_name, 1);
        head->addWidget(m_status);

        m_temp = new QLabel(QStringLiteral("--"));
        m_temp->setObjectName("zoneTemp");
        m_metrics = new QLabel;
        m_metrics->setObjectName("muted");
        m_equipment = new QLabel;
        m_equipment->setObjectName("muted");
        m_equipment->setWordWrap(true);

        lay->addLayout(head);
        lay->addWidget(m_temp);
        lay->addWidget(m_metrics);
        lay->addWidget(m_equipment);
    }

    void setContent(const QString& temp, const QString& metrics, const QString& equipment,
                    const QString& level, const QString& status, bool selected) {
        m_temp->setText(temp);
        m_metrics->setText(metrics);
        m_equipment->setText(equipment);
        m_status->setText(status);
        m_status->setProperty("level", level);
        repolish(m_status);
        setProperty("selected", selected);
        repolish(this);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_onClick) m_onClick();
        QFrame::mousePressEvent(event);
    }

private:
    std::function<void()> m_onClick;
    QLabel* m_name = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_temp = nullptr;
    QLabel* m_metrics = nullptr;
    QLabel* m_equipment = nullptr;
};

// Time-series chart: grid, time axis, optional threshold line, and latest value.
// X values are epoch milliseconds.
class TrendChartWidget : public QWidget {
public:
    TrendChartWidget(const QString& title, const QString& unit, QColor color, QWidget* parent = nullptr)
        : QWidget(parent), m_title(title), m_unit(unit), m_color(color) {
        setMinimumHeight(230);
    }

    void setPoints(const QList<QPointF>& points) {
        m_points = points;
        update();
    }

    void setColors(const Palette& palette) {
        m_pal = palette;
        update();
    }

    void setThreshold(double value, const QString& label) {
        m_threshold = value;
        m_thresholdLabel = label;
        m_hasThreshold = true;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Card background.
        p.setPen(QPen(QColor(m_pal.border), 1));
        p.setBrush(QColor(m_pal.table));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(QColor(m_pal.text));
        p.drawText(QRectF(16, 12, width() - 32, 22), Qt::AlignLeft | Qt::AlignVCenter, m_title);

        const QRectF plot(56, 50, width() - 72, height() - 82);

        if (m_points.size() < 2) {
            p.setFont(font());
            p.setPen(QColor(m_pal.muted));
            p.drawText(plot, Qt::AlignCenter, QStringLiteral("Waiting for data..."));
            return;
        }

        const QPointF last = m_points.last();
        p.setFont(bold);
        p.setPen(m_color);
        p.drawText(QRectF(16, 12, width() - 32, 22), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 %2").arg(QString::number(last.y(), 'f', 1), m_unit));

        // Y range, padded, and always including the threshold.
        double minY = m_points.first().y();
        double maxY = minY;
        for (const auto& pt : m_points) {
            minY = std::min(minY, pt.y());
            maxY = std::max(maxY, pt.y());
        }
        if (m_hasThreshold) maxY = std::max(maxY, m_threshold);
        const double pad = std::max(0.5, (maxY - minY) * 0.1);
        minY -= pad;
        maxY += pad;

        const double minX = m_points.first().x();
        const double spanX = std::max(1.0, m_points.last().x() - minX);
        const int decimals = (maxY - minY) > 20.0 ? 0 : 1;

        auto yOf = [&](double v) {
            return plot.bottom() - (v - minY) / (maxY - minY) * plot.height();
        };
        auto toPixel = [&](const QPointF& pt) {
            return QPointF(plot.left() + (pt.x() - minX) / spanX * plot.width(), yOf(pt.y()));
        };

        // Grid and Y-axis labels.
        p.setFont(font());
        for (int i = 0; i <= 4; ++i) {
            const double v = minY + (maxY - minY) * i / 4.0;
            const double y = plot.bottom() - plot.height() * i / 4.0;
            p.setPen(QPen(QColor(138, 155, 184, 35), 1));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(QColor(m_pal.muted));
            p.drawText(QRectF(4, y - 9, 46, 18), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(v, 'f', decimals));
        }

        // Time labels along the X axis.
        for (int i = 0; i <= 3; ++i) {
            const double x = plot.left() + plot.width() * i / 3.0;
            const qint64 ms = static_cast<qint64>(minX + spanX * i / 3.0);
            const QString label = QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm"));
            p.setPen(QColor(m_pal.muted));
            p.drawText(QRectF(x - 30, plot.bottom() + 6, 60, 16), Qt::AlignCenter, label);
        }

        // Threshold line.
        if (m_hasThreshold) {
            const double y = yOf(m_threshold);
            p.setPen(QPen(QColor("#ef4444"), 1.2, Qt::DashLine));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.drawText(QRectF(plot.right() - 150, y - 18, 148, 16), Qt::AlignRight | Qt::AlignVCenter,
                       m_thresholdLabel);
        }

        // Area fill and curve.
        QPainterPath line;
        line.moveTo(toPixel(m_points.first()));
        for (qsizetype i = 1; i < m_points.size(); ++i) line.lineTo(toPixel(m_points[i]));

        QPainterPath area = line;
        area.lineTo(plot.right(), plot.bottom());
        area.lineTo(plot.left(), plot.bottom());
        area.closeSubpath();

        QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
        QColor top = m_color;
        top.setAlpha(70);
        QColor bottom = m_color;
        bottom.setAlpha(0);
        gradient.setColorAt(0.0, top);
        gradient.setColorAt(1.0, bottom);
        p.fillPath(area, QBrush(gradient));

        p.setPen(QPen(m_color, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);

        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawEllipse(toPixel(last), 4, 4);
    }

private:
    QString m_title;
    QString m_unit;
    QColor m_color;
    QList<QPointF> m_points;
    double m_threshold = 0.0;
    QString m_thresholdLabel;
    bool m_hasThreshold = false;
    Palette m_pal = darkPalette();
};

// One room on the floor plan, ready to draw.
struct FloorRoomView {
    QString zone;
    QString label;
    QRectF rect;               // fractions of the plan, 0 to 1
    QString level;             // "ok", "warning", "critical" or "nodata"
    QStringList lines;         // detail lines drawn inside the room
    bool selected = false;
};

// Floor plan: rooms drawn as rectangles, coloured by status. Clicking a room selects its zone.
class FloorPlanWidget : public QWidget {
public:
    using SelectFn = std::function<void(const QString&)>;

    explicit FloorPlanWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(380);
        setMouseTracking(true);
    }

    void setRooms(const QList<FloorRoomView>& rooms) {
        m_rooms = rooms;
        update();
    }

    void setOnSelect(SelectFn fn) { m_onSelect = std::move(fn); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#0f1830"));

        if (m_rooms.isEmpty()) {
            p.setPen(QColor("#8a9bb8"));
            p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No floor plan loaded (config/floorplan.json)"));
            return;
        }

        for (const FloorRoomView& r : m_rooms) {
            const QRectF box = pixelRect(r.rect);
            const QColor edge = levelColor(r.level);
            QColor fill = edge;
            fill.setAlpha(30);
            p.setPen(QPen(r.selected ? QColor("#22d3ee") : edge, r.selected ? 3.0 : 2.0));
            p.setBrush(fill);
            p.drawRoundedRect(box, 10, 10);

            QFont bold = font();
            bold.setBold(true);
            p.setFont(bold);
            p.setPen(QColor("#e5ecf6"));
            p.drawText(box.adjusted(12, 10, -12, 0), Qt::AlignTop | Qt::AlignLeft, r.label);

            p.setFont(font());
            p.setPen(QColor("#b8c6dc"));
            p.drawText(box.adjusted(12, 36, -12, -8), Qt::AlignTop | Qt::AlignLeft, r.lines.join(QStringLiteral("\n")));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const FloorRoomView* room = roomAt(event->position());
        setCursor(room ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(room ? room->label + QStringLiteral("\n") + room->lines.join(QStringLiteral("\n")) : QString());
        QWidget::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (const FloorRoomView* room = roomAt(event->position()); room && m_onSelect)
                m_onSelect(room->zone);
        }
        QWidget::mousePressEvent(event);
    }

private:
    QRectF pixelRect(const QRectF& n) const {
        const QRectF area = QRectF(rect()).adjusted(14, 14, -14, -14);
        return QRectF(area.left() + n.x() * area.width(), area.top() + n.y() * area.height(),
                      n.width() * area.width(), n.height() * area.height())
            .adjusted(4, 4, -4, -4);
    }

    const FloorRoomView* roomAt(const QPointF& pos) const {
        for (const FloorRoomView& r : m_rooms) {
            if (pixelRect(r.rect).contains(pos)) return &r;
        }
        return nullptr;
    }

    static QColor levelColor(const QString& level) {
        if (level == QLatin1String("critical")) return QColor("#ef4444");
        if (level == QLatin1String("warning")) return QColor("#f59e0b");
        if (level == QLatin1String("ok")) return QColor("#22c55e");
        return QColor("#8a9bb8");
    }

    QList<FloorRoomView> m_rooms;
    SelectFn m_onSelect;
};
