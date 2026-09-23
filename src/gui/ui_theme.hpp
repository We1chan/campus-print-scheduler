#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

namespace cps::gui::ui {
enum class Icon { Printer, Document, Plus, Import, Export, Folder, Pause, Play, Refresh, Activity, Search };

inline QIcon icon(Icon kind, const QColor& color = QColor("#718095")) {
    QPixmap canvas(48, 48);
    canvas.setDevicePixelRatio(2);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case Icon::Printer:
        p.drawRoundedRect(QRectF(3, 8, 18, 10), 2, 2);
        p.drawRect(QRectF(7, 3, 10, 5));
        p.setBrush(QColor("#FFFFFF"));
        p.drawRect(QRectF(7, 14, 10, 7));
        p.drawPoint(QPointF(17.5, 11));
        break;
    case Icon::Document: {
        QPainterPath path;
        path.moveTo(6, 3); path.lineTo(14, 3); path.lineTo(19, 8);
        path.lineTo(19, 21); path.lineTo(6, 21); path.closeSubpath();
        p.drawPath(path); p.drawLine(14, 3, 14, 8); p.drawLine(14, 8, 19, 8);
        p.drawLine(9, 12, 16, 12); p.drawLine(9, 16, 14, 16);
        break;
    }
    case Icon::Plus:
        p.drawLine(12, 5, 12, 19); p.drawLine(5, 12, 19, 12); break;
    case Icon::Import:
    case Icon::Export: {
        const bool down = kind == Icon::Export;
        p.drawLine(12, 3, 12, 15);
        const int tip = down ? 15 : 3;
        const int base = down ? 10 : 8;
        p.drawLine(7, base, 12, tip); p.drawLine(12, tip, 17, base);
        p.drawLine(4, 15, 4, 20); p.drawLine(4, 20, 20, 20); p.drawLine(20, 20, 20, 15);
        break;
    }
    case Icon::Folder: {
        QPainterPath path;
        path.moveTo(3, 6); path.lineTo(9, 6); path.lineTo(11, 9); path.lineTo(21, 9);
        path.lineTo(21, 20); path.lineTo(3, 20); path.closeSubpath(); p.drawPath(path); break;
    }
    case Icon::Pause: p.drawLine(8, 5, 8, 19); p.drawLine(16, 5, 16, 19); break;
    case Icon::Play: {
        QPainterPath path; path.moveTo(8, 4); path.lineTo(20, 12); path.lineTo(8, 20); path.closeSubpath();
        p.drawPath(path); break;
    }
    case Icon::Refresh:
        p.drawArc(QRectF(4, 4, 16, 16), 40 * 16, 280 * 16);
        p.drawLine(19, 3, 19, 9); p.drawLine(13, 9, 19, 9); break;
    case Icon::Activity: {
        QPainterPath path; path.moveTo(2, 12); path.lineTo(6, 12); path.lineTo(9, 5);
        path.lineTo(14, 19); path.lineTo(17, 12); path.lineTo(22, 12); p.drawPath(path); break;
    }
    case Icon::Search: p.drawEllipse(QRectF(4, 3, 12, 12)); p.drawLine(15, 14, 21, 20); break;
    }
    return QIcon(canvas);
}

inline QString styleSheet() {
    return QStringLiteral(R"(
QMainWindow, QWidget#workspace { background: #F7F8FB; color: #202737; }
QWidget { font-size: 13px; }
QLabel { color: #202737; background: transparent; border: none; }
QLabel[role="muted"], QLabel[role="metricHint"] { color: #718095; font-size: 11px; }
QLabel[role="section"] { color: #202737; font-size: 18px; font-weight: 600; }
QLabel[role="eyebrow"] { color: #718095; font-size: 11px; }
QLabel#pageTitle { font-size: 21px; font-weight: 600; }
QLabel[role="metricValue"] { font-size: 26px; font-weight: 600; }
QLabel[role="metricLabel"] { color: #718095; font-size: 12px; }
QFrame[role="card"], QFrame[role="metric"] { background: white; border: 1px solid #E5E9F1; border-radius: 10px; }
QLabel[role="badge"] { color: #61728B; padding: 3px 8px; font-size: 11px; }
QLabel[tone="ready"] { color: #3C8B7D; }
QLabel[tone="waiting"] { color: #A37437; }
QLabel[tone="error"] { color: #C05D54; }
QPushButton, QToolButton { color: #59667C; background: white; border: 1px solid #E1E6EF; border-radius: 7px; padding: 8px 12px; }
QPushButton:hover, QToolButton:hover { color: #285FDB; border-color: #BDCEF4; background: #F2F6FF; }
QPushButton:pressed, QToolButton:pressed { background: #E6EDFB; }
QPushButton:focus, QToolButton:focus { border-color: #729AF3; }
QPushButton:disabled, QToolButton:disabled { color: #A3ADBE; background: #F6F7FA; border-color: #EAEDF3; }
QPushButton[role="primary"] { color: white; background: #3569ED; border-color: #3569ED; font-weight: 600; padding: 10px 17px; }
QPushButton[role="primary"]:hover { background: #245AD9; }
QPushButton[role="quiet"], QToolButton[role="quiet"] { border-color: transparent; background: transparent; }
QPushButton[role="nav"] { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 16px 18px; }
QPushButton[role="nav"]:checked { background: transparent; color: #285FDB; border-bottom-color: #3569ED; font-weight: 600; }
QPushButton[role="danger"]:enabled { color: #C15B56; }
QPushButton:checked { background: #E9EFFC; color: #3569ED; border-color: #D6E2FC; }
QPushButton[role="queueHit"] { background: transparent; border: 1px solid transparent; border-radius: 5px; }
QPushButton[role="queueHit"]:hover { background: rgba(53,105,237,8); border-color: #DEE7F8; }
QPushButton[role="queueHit"]:focus { border-color: #8BABF0; }
QLineEdit, QSpinBox, QComboBox { background: white; color: #293449; border: 1px solid #DCE3EF; border-radius: 7px; padding: 9px 10px; selection-background-color: #DCE7FE; selection-color: #1D459A; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #729AF3; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: none; }
QSpinBox { padding-right: 19px; }
QSpinBox::up-button, QSpinBox::down-button { width: 17px; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { image: none; }
QDialog { background: #F8F9FC; }
QListWidget { background: white; border: 1px solid #E4E9F1; color: #293449; padding: 5px; outline: none; }
QListWidget::item { padding: 13px; border-bottom: 1px solid #F0F2F6; }
QListWidget::item:selected { background: #EAF0FC; color: #3569ED; }
QComboBox QAbstractItemView { background: white; color: #293449; selection-background-color: #EAF0FC; selection-color: #3569ED; }
QProgressBar { background: #E3E8F1; border: none; border-radius: 3px; min-height: 6px; max-height: 6px; }
QProgressBar::chunk { background: #3569ED; border-radius: 3px; }
QTableView { background: white; border: none; color: #28344A; selection-background-color: #EAF0FC; selection-color: #28344A; outline: none; }
QTableView::item { padding: 8px; border-bottom: 1px solid #E9EDF4; }
QHeaderView::section { background: #F7F9FC; color: #718095; border: none; border-bottom: 1px solid #E6EAF1; padding: 10px 8px; font-size: 11px; }
QScrollArea, QStackedWidget { border: none; background: transparent; }
QWidget#queueContent { background: transparent; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
QScrollBar::handle:vertical { background: #CBD3E1; border-radius: 3px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 7px; }
QScrollBar::handle:horizontal { background: #CBD3E1; border-radius: 3px; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QPlainTextEdit { background: #F1F4F9; color: #586B85; border: 1px solid #DEE5EF; border-radius: 7px; padding: 8px; font-size: 11px; }
QToolTip { background: #263449; color: white; border: none; padding: 6px 9px; }
)");
}
} // namespace cps::gui::ui
