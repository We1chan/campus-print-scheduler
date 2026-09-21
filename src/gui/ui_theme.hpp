#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

namespace cps::gui::ui {
enum class Icon { Printer, Document, Plus, Import, Export, Folder, Pause, Play, Refresh, Activity, Search };

inline QIcon icon(Icon kind, const QColor& color = QColor("#657188")) {
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
QMainWindow, QWidget#workspace { background: #F4F6FA; color: #202A3C; }
QWidget { font-size: 13px; }
QLabel { color: #263248; background: transparent; border: none; }
QLabel[role="muted"] { color: #8892A4; font-size: 12px; }
QLabel[role="section"] { color: #202A3C; font-size: 15px; font-weight: 600; }
QLabel[role="eyebrow"] { color: #7C879B; font-size: 11px; font-weight: 600; }
QLabel#pageTitle { color: #192338; font-size: 26px; font-weight: 600; }
QLabel#brandMark { background: #365BEA; border-radius: 12px; }
QFrame[role="card"] { background: #FFFFFF; border: 1px solid #E5E9F1; border-radius: 14px; }
QFrame[role="metric"] { background: #FFFFFF; border: 1px solid #E5E9F1; border-radius: 12px; }
QLabel[role="metricValue"] { color: #202A3C; font-size: 28px; font-weight: 600; }
QLabel[role="metricLabel"] { color: #788399; font-size: 12px; }
QLabel[role="metricHint"] { color: #98A1B1; font-size: 11px; }
QLabel[role="iconTile"] { background: #F0F3FB; border-radius: 10px; }
QLabel[role="badge"] { background: #ECF1FF; color: #4A67CC; border-radius: 10px; padding: 4px 10px; font-size: 11px; }
QLabel[role="badge"][tone="ready"] { background: #EAF6F0; color: #28855E; }
QLabel[role="badge"][tone="waiting"] { background: #FFF5E5; color: #A77A2F; }
QLabel[role="badge"][tone="error"] { background: #FCEEEE; color: #BD5656; }
QLabel[role="printerTitle"] { font-size: 16px; font-weight: 600; }
QPushButton, QToolButton { color: #56647B; background: #FFFFFF; border: 1px solid #E1E6EF; border-radius: 8px; padding: 8px 12px; }
QPushButton:hover, QToolButton:hover { background: #F6F8FE; border-color: #C5CFE4; color: #2F4FBD; }
QPushButton:pressed, QToolButton:pressed { background: #EAF0FD; }
QPushButton:focus, QToolButton:focus { border-color: #6F8BED; }
QPushButton:disabled, QToolButton:disabled { color: #B5BDCA; background: #F9FAFC; border-color: #EDF0F5; }
QPushButton[role="primary"] { background: #365BEA; border-color: #365BEA; color: white; font-weight: 600; padding: 11px 14px; }
QPushButton[role="primary"]:hover { background: #2E50D4; border-color: #2E50D4; }
QPushButton[role="primary"]:pressed { background: #2845B5; }
QPushButton[role="quiet"], QToolButton[role="quiet"] { background: transparent; border-color: transparent; padding: 6px 9px; }
QPushButton[role="quiet"]:hover, QToolButton[role="quiet"]:hover { background: #EBEFF7; color: #365BEA; }
QPushButton[role="danger"]:enabled { color: #BF6464; }
QPushButton[role="danger"]:enabled:hover { color: #AC4040; background: #FFF2F2; border-color: #F1CDCD; }
QPushButton:checked { background: #FFF4E1; border-color: #EFDBB6; color: #926D31; }
QLineEdit, QSpinBox, QComboBox { background: #FBFCFE; color: #354159; border: 1px solid #DFE5F0; border-radius: 8px; padding: 8px 10px; selection-background-color: #DCE6FF; selection-color: #253C82; }
QLineEdit:hover, QSpinBox:hover, QComboBox:hover { border-color: #C6D1E8; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { background: white; border-color: #6280E8; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { width: 24px; border: none; }
QComboBox::down-arrow { image: none; }
QComboBox QAbstractItemView { border: 1px solid #DFE5F0; background: white; selection-background-color: #ECF1FF; selection-color: #365BEA; padding: 4px; }
QSpinBox { padding-right: 20px; }
QSpinBox::up-button, QSpinBox::down-button { width: 18px; border: none; background: transparent; }
QSpinBox::up-arrow, QSpinBox::down-arrow { image: none; }
QProgressBar { background: #EDF1F7; border: none; border-radius: 3px; min-height: 6px; max-height: 6px; }
QProgressBar::chunk { background: #4C6EEE; border-radius: 3px; }
QTableView { background: #FFFFFF; alternate-background-color: #FFFFFF; border: none; color: #354159; gridline-color: #F0F2F6; outline: none; selection-background-color: #EFF4FF; selection-color: #273D74; }
QTableView::item { padding: 8px; border-bottom: 1px solid #F0F2F6; }
QTableView::item:hover { background: #F7F9FE; }
QHeaderView::section { background: #F7F9FC; color: #929BAD; border: none; border-bottom: 1px solid #EEF1F6; padding: 10px 8px; font-size: 11px; font-weight: 400; }
QScrollArea { border: none; background: transparent; }
QWidget#sideContent, QWidget#queueContent { background: transparent; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
QScrollBar::handle:vertical { background: #DCE2ED; border-radius: 3px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 7px; margin: 0; }
QScrollBar::handle:horizontal { background: #DCE2ED; border-radius: 3px; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QPlainTextEdit { background: #F8FAFD; color: #758096; border: 1px solid #E5EAF3; border-radius: 9px; padding: 8px; font-size: 11px; }
QToolTip { background: #263248; color: white; border: none; padding: 6px 9px; }
)");
}
} // namespace cps::gui::ui
