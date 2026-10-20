#pragma once

// Colour themes and the stylesheet built from them. Widgets refer to colours by
// object name, so switching theme only means rebuilding this stylesheet.

#include <QList>
#include <QPair>
#include <QString>

struct Palette {
    QString name;
    QString bg;             // window background
    QString header;         // header bar and status bar
    QString panel;          // cards and panels
    QString border;
    QString text;
    QString muted;
    QString accent;
    QString input;          // combo boxes, spin boxes
    QString inputBorder;
    QString table;
    QString tableAlt;
    QString grid;
    QString selection;
    QString selectionText;
    QString cardSelected;
    QString hover;
    QString okBg;
    QString warnBg;
    QString critBg;
    QString nodataBg;
    QString ok;
    QString warn;
    QString crit;
};

inline Palette darkPalette() {
    Palette p;
    p.name = QStringLiteral("dark");
    p.bg = QStringLiteral("#0b1220");
    p.header = QStringLiteral("#0f1830");
    p.panel = QStringLiteral("#142038");
    p.border = QStringLiteral("#22304d");
    p.text = QStringLiteral("#e5ecf6");
    p.muted = QStringLiteral("#8a9bb8");
    p.accent = QStringLiteral("#22d3ee");
    p.input = QStringLiteral("#0f1830");
    p.inputBorder = QStringLiteral("#2d3d5e");
    p.table = QStringLiteral("#0f1830");
    p.tableAlt = QStringLiteral("#122039");
    p.grid = QStringLiteral("#1c2a47");
    p.selection = QStringLiteral("#1e3a66");
    p.selectionText = QStringLiteral("#ffffff");
    p.cardSelected = QStringLiteral("#163056");
    p.hover = QStringLiteral("#1a2740");
    p.okBg = QStringLiteral("#0f2a1d");
    p.warnBg = QStringLiteral("#2d2110");
    p.critBg = QStringLiteral("#33141a");
    p.nodataBg = QStringLiteral("#1a2740");
    p.ok = QStringLiteral("#22c55e");
    p.warn = QStringLiteral("#f59e0b");
    p.crit = QStringLiteral("#ef4444");
    return p;
}

inline Palette lightPalette() {
    Palette p;
    p.name = QStringLiteral("light");
    p.bg = QStringLiteral("#eef3f9");
    p.header = QStringLiteral("#ffffff");
    p.panel = QStringLiteral("#ffffff");
    p.border = QStringLiteral("#d3dde9");
    p.text = QStringLiteral("#1b2636");
    p.muted = QStringLiteral("#5b6b82");
    p.accent = QStringLiteral("#0e7490");
    p.input = QStringLiteral("#ffffff");
    p.inputBorder = QStringLiteral("#c3cfdf");
    p.table = QStringLiteral("#ffffff");
    p.tableAlt = QStringLiteral("#f5f8fc");
    p.grid = QStringLiteral("#e3eaf3");
    p.selection = QStringLiteral("#cfe3ff");
    p.selectionText = QStringLiteral("#0b1220");
    p.cardSelected = QStringLiteral("#e6f4fb");
    p.hover = QStringLiteral("#eef3fa");
    p.okBg = QStringLiteral("#dcfce7");
    p.warnBg = QStringLiteral("#fef3c7");
    p.critBg = QStringLiteral("#fee2e2");
    p.nodataBg = QStringLiteral("#e2e8f0");
    p.ok = QStringLiteral("#15803d");
    p.warn = QStringLiteral("#b45309");
    p.crit = QStringLiteral("#b91c1c");
    return p;
}

inline QString buildStyleSheet(const Palette& p) {
    static const char* const kTemplate = R"(
QWidget#root { background: @bg@; }
QWidget { color: @text@; font-family: "Segoe UI", "Inter", sans-serif; font-size: 13px; }
QFrame#header { background: @header@; border-bottom: 1px solid @border@; }
QLabel#appTitle { font-size: 17px; font-weight: 700; }
QLabel#clock { color: @muted@; font-family: "Consolas", monospace; font-size: 14px; padding-left: 8px; }
QLabel#sectionTitle { font-size: 14px; font-weight: 600; }
QLabel#muted { color: @muted@; }
QFrame#kpi, QFrame#zoneCard, QFrame#panel { background: @panel@; border: 1px solid @border@; border-radius: 10px; }
QFrame#zoneCard[selected="true"] { border: 1px solid @accent@; background: @cardSelected@; }
QLabel#kpiTitle { color: @muted@; font-size: 11px; font-weight: 600; }
QLabel#kpiValue { font-size: 26px; font-weight: 700; }
QLabel#kpiValue[level="ok"] { color: @ok@; }
QLabel#kpiValue[level="warning"] { color: @warn@; }
QLabel#kpiValue[level="critical"] { color: @crit@; }
QLabel#zoneName { font-size: 14px; font-weight: 600; }
QLabel#zoneTemp { font-size: 28px; font-weight: 700; }
QLabel#zoneStatus { font-size: 11px; font-weight: 700; padding: 3px 8px; border-radius: 8px; }
QLabel#zoneStatus[level="ok"] { color: @ok@; background: @okBg@; }
QLabel#zoneStatus[level="warning"] { color: @warn@; background: @warnBg@; }
QLabel#zoneStatus[level="critical"] { color: @crit@; background: @critBg@; }
QLabel#zoneStatus[level="nodata"] { color: @muted@; background: @nodataBg@; }
QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: @muted@; padding: 12px 22px; border: none;
               border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: @accent@; border-bottom: 2px solid @accent@; }
QTabBar::tab:hover { color: @text@; }
QPushButton { background: #2563eb; color: #ffffff; border: none; border-radius: 6px;
              padding: 8px 16px; font-weight: 600; }
QPushButton:hover { background: #1d4ed8; }
QPushButton:disabled { background: @hover@; color: @muted@; }
QPushButton#ghost { background: transparent; color: @text@; border: 1px solid @inputBorder@; }
QPushButton#ghost:hover { background: @hover@; }
QComboBox, QDoubleSpinBox, QDateEdit { background: @input@; color: @text@; border: 1px solid @inputBorder@;
                            border-radius: 6px; padding: 6px 10px; min-height: 18px; }
QComboBox QAbstractItemView { background: @input@; color: @text@; border: 1px solid @inputBorder@;
                              selection-background-color: @selection@; }
QTableWidget { background: @table@; alternate-background-color: @tableAlt@; border: 1px solid @border@;
               border-radius: 8px; gridline-color: @grid@; selection-background-color: @selection@;
               selection-color: @selectionText@; }
QHeaderView::section { background: @panel@; color: @muted@; border: none;
                       border-bottom: 1px solid @border@; padding: 8px; font-weight: 600; }
QTabWidget, QStackedWidget, QStackedWidget > QWidget,
QScrollArea, QScrollArea > QWidget > QWidget { background: @bg@; }
QScrollArea { border: none; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: @inputBorder@; border-radius: 5px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QStatusBar { background: @header@; color: @muted@; border-top: 1px solid @border@; }
)";

    QString sheet = QString::fromUtf8(kTemplate);
    const QList<QPair<QString, QString>> tokens = {
        {"@bg@", p.bg}, {"@header@", p.header}, {"@panel@", p.panel}, {"@border@", p.border},
        {"@text@", p.text}, {"@muted@", p.muted}, {"@accent@", p.accent}, {"@input@", p.input},
        {"@inputBorder@", p.inputBorder}, {"@table@", p.table}, {"@tableAlt@", p.tableAlt},
        {"@grid@", p.grid}, {"@selection@", p.selection}, {"@selectionText@", p.selectionText},
        {"@cardSelected@", p.cardSelected}, {"@hover@", p.hover}, {"@okBg@", p.okBg},
        {"@warnBg@", p.warnBg}, {"@critBg@", p.critBg}, {"@nodataBg@", p.nodataBg},
        {"@ok@", p.ok}, {"@warn@", p.warn}, {"@crit@", p.crit},
    };
    for (const auto& t : tokens) sheet.replace(t.first, t.second);
    return sheet;
}
