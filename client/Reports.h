#pragma once

// Writes CSV and HTML files. Both take plain strings, so they are independent of the window.

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>

namespace reports {

// CSV with a UTF-8 byte-order mark, so Excel shows characters such as °C correctly.
inline QByteArray toCsv(const QStringList& header, const QList<QStringList>& rows) {
    auto field = [](QString s) {
        if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) ||
            s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'))) {
            s.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            s = QStringLiteral("\"") + s + QStringLiteral("\"");
        }
        return s;
    };
    QStringList lines;
    QStringList cells;
    for (const QString& h : header) cells << field(h);
    lines << cells.join(QLatin1Char(','));
    for (const QStringList& row : rows) {
        cells.clear();
        for (const QString& c : row) cells << field(c);
        lines << cells.join(QLatin1Char(','));
    }
    QByteArray out("\xEF\xBB\xBF");
    out += lines.join(QStringLiteral("\r\n")).toUtf8();
    out += "\r\n";
    return out;
}

// A self-contained HTML page: title, notes, and one table.
inline QByteArray toHtml(const QString& title, const QStringList& notes,
                         const QStringList& headers, const QList<QStringList>& rows) {
    QString html;
    html += QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>%1</title><style>"
                           "body{font-family:Segoe UI,Arial,sans-serif;margin:32px;color:#1b2636}"
                           "h1{font-size:22px;margin-bottom:4px}ul{color:#5b6b82}"
                           "table{border-collapse:collapse;width:100%;margin-top:16px}"
                           "th,td{border:1px solid #d3dde9;padding:8px 10px;text-align:left;font-size:13px}"
                           "th{background:#eef3f9}</style></head><body>")
                .arg(title.toHtmlEscaped());
    html += QStringLiteral("<h1>%1</h1><ul>").arg(title.toHtmlEscaped());
    for (const QString& n : notes) html += QStringLiteral("<li>%1</li>").arg(n.toHtmlEscaped());
    html += QStringLiteral("</ul><table><tr>");
    for (const QString& h : headers) html += QStringLiteral("<th>%1</th>").arg(h.toHtmlEscaped());
    html += QStringLiteral("</tr>");
    for (const QStringList& row : rows) {
        html += QStringLiteral("<tr>");
        for (const QString& c : row) html += QStringLiteral("<td>%1</td>").arg(c.toHtmlEscaped());
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table></body></html>");
    return html.toUtf8();
}

inline bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(data) == data.size();
}

}  // namespace reports
