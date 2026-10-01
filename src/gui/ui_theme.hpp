#pragma once

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <utility>

namespace cps::gui::ui {
enum class Icon { Printer, Document, Plus, Import, Export, Folder, Pause, Play, Refresh, Activity, Search, Check };

inline QIcon icon(Icon kind, const QColor& color = QColor("#62687D")) {
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
        p.setBrush(color == QColor(Qt::white) ? QColor("#4548E8") : QColor("#FFFFFF"));
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
    case Icon::Check: {
        QPainterPath path; path.moveTo(5, 12.5); path.lineTo(10, 17.5); path.lineTo(19, 7); p.drawPath(path); break;
    }
    }
    return QIcon(canvas);
}

inline QString styleSheet() {
    return QStringLiteral(R"(
QMainWindow, QWidget#workspace { background: #FAFBFF; color: #24273B; }
QWidget { font-size: 13px; }
QLabel { color: #24273B; background: transparent; border: none; }
QLabel[role="muted"], QLabel[role="metricHint"] { color: #62687D; font-size: 11px; }
QLabel[role="body"] { color: #4A566B; font-size: 12px; }
QLabel[role="section"] { color: #24273B; font-size: 18px; font-weight: 600; }
QLabel[role="cardTitle"] { color: #24273B; font-size: 14px; font-weight: 600; }
QLabel[role="eyebrow"] { color: #62687D; font-size: 11px; }
QLabel#pageTitle { font-size: 17px; font-weight: 600; }
QLabel#brandMark { background: #4548E8; border-radius: 9px; }
QLabel[role="metricValue"] { font-size: 26px; font-weight: 600; }
QLabel[role="metricLabel"] { color: #62687D; font-size: 12px; }
QLabel[role="statLabel"] { color: #62687D; font-size: 11px; }
QLabel[role="statValue"] { font-size: 17px; font-weight: 600; }
QFrame[role="card"], QFrame[role="metric"] { background: white; border: 1px solid #E5E9F1; border-radius: 10px; }
QFrame[role="summary"] { background: #F4F5FE; border: 1px solid #E1E8F6; border-radius: 8px; }
QLabel[role="badge"] { color: #61728B; background: #EEF1F6; border-radius: 14px; padding: 3px 12px; font-size: 11px; font-weight: 600; }
QLabel[tone="ready"] { color: #2F7D6E; }
QLabel[tone="waiting"] { color: #94682C; }
QLabel[tone="error"] { color: #B9524A; }
QLabel[role="badge"][tone="ready"] { background: #E6F4EF; }
QLabel[role="badge"][tone="waiting"] { background: #FAF2E4; }
QLabel[role="badge"][tone="error"] { background: #FBEDEA; }
QPushButton, QToolButton { color: #59667C; background: white; border: 1px solid #E1E6EF; border-radius: 7px; padding: 8px 12px; }
QPushButton:hover, QToolButton:hover { color: #373ADB; border-color: #BDCEF4; background: #F4F5FE; }
QPushButton:pressed, QToolButton:pressed { background: #E1E3FF; }
QPushButton:focus, QToolButton:focus { border-color: #4548E8; }
QPushButton:disabled, QToolButton:disabled { color: #A3ADBE; background: #F6F7FA; border-color: #EAEDF3; }
QPushButton[role="primary"] { color: white; background: #4548E8; border-color: #4548E8; font-weight: 600; padding: 10px 17px; }
QPushButton[role="primary"]:hover { background: #373ADB; border-color: #373ADB; }
QPushButton[role="primary"]:pressed { background: #2D30C2; border-color: #2D30C2; }
QPushButton[role="primary"]:disabled { color: white; background: #B7CFC6; border-color: #B7CFC6; }
QPushButton[role="quiet"], QToolButton[role="quiet"] { border-color: transparent; background: transparent; }
QPushButton[role="quiet"]:hover, QToolButton[role="quiet"]:hover { background: #EEEEFC; }
QPushButton[role="nav"] { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 16px 18px; }
QPushButton[role="nav"]:hover { color: #373ADB; background: transparent; }
QPushButton[role="nav"]:checked { background: transparent; color: #373ADB; border-bottom-color: #4548E8; font-weight: 600; }
QPushButton[role="danger"]:enabled { color: #C15B56; }
QPushButton[role="danger"]:enabled:hover { color: #B4524A; background: #FDF1EF; border-color: #EFC4BF; }
QPushButton[role="dangerPrimary"] { color: white; background: #C95B53; border-color: #C95B53; font-weight: 600; padding: 10px 17px; }
QPushButton[role="dangerPrimary"]:hover { background: #B84E46; border-color: #B84E46; }
QPushButton:checked { background: #E9EFFC; color: #4548E8; border-color: #D6E2FC; }
QPushButton[role="queueHit"] { background: transparent; border: 1px solid transparent; border-radius: 5px; }
QPushButton[role="queueHit"]:hover { background: rgba(53,105,237,8); border-color: #DEE7F8; }
QPushButton[role="queueHit"]:focus { border-color: #4548E8; }
QLineEdit, QSpinBox, QComboBox { background: white; color: #293449; border: 1px solid #DCE3EF; border-radius: 7px; padding: 9px 10px; selection-background-color: #E1E3FF; selection-color: #1D459A; }
QLineEdit:hover, QSpinBox:hover, QComboBox:hover { border-color: #C3CFE4; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #4548E8; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: #A3ADBE; background: #F6F7FA; border-color: #EAEDF3; }
QComboBox { padding-right: 24px; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: none; }
QSpinBox { padding-right: 19px; }
QSpinBox::up-button, QSpinBox::down-button { width: 17px; border: none; }
QSpinBox::up-arrow, QSpinBox::down-arrow { image: none; }
QDialog { background: #F8F9FC; }
QListWidget { background: white; border: 1px solid #E4E9F1; border-radius: 8px; color: #293449; padding: 5px; outline: none; }
QListWidget::item { padding: 11px 12px; border-radius: 6px; }
QListWidget::item:hover { background: #F4F5FE; }
QListWidget::item:selected { background: #EEEEFC; color: #373ADB; }
QComboBox QAbstractItemView { background: white; color: #293449; selection-background-color: #EEEEFC; selection-color: #4548E8; }
QProgressBar { background: #E3E8F1; border: none; border-radius: 3px; min-height: 6px; max-height: 6px; }
QProgressBar::chunk { background: #4548E8; border-radius: 3px; }
QTableView { background: white; border: none; color: #28344A; selection-background-color: #EEEEFC; selection-color: #28344A; outline: none; }
QTableView::item { padding: 8px; border-bottom: 1px solid #E9EDF4; }
QHeaderView::section { background: #F7F9FC; color: #62687D; border: none; border-bottom: 1px solid #E6EAF1; padding: 10px 8px; font-size: 11px; }
QScrollArea, QStackedWidget { border: none; background: transparent; }
QWidget#queueContent, QWidget#shopContent, QWidget[role="page"] { background: transparent; }
QWidget#workbench { background: white; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 0; }
QScrollBar::handle:vertical { background: #CBD3E1; border-radius: 3px; min-height: 28px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 7px; }
QScrollBar::handle:horizontal { background: #CBD3E1; border-radius: 3px; min-width: 28px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QPlainTextEdit { background: #F1F4F9; color: #586B85; border: 1px solid #DEE5EF; border-radius: 7px; padding: 8px; font-size: 11px; }
QToolTip { background: #263449; color: white; border: none; padding: 6px 9px; }
QLabel[role="step"] { color: #4548E8; font-size: 12px; font-weight: 600; }
QLabel[role="price"] { font-size: 30px; font-weight: 600; }
QLabel[role="eta"] { color: #373ADB; font-size: 15px; font-weight: 600; }
QLabel[role="eta"][tone="waiting"] { color: #A37437; }
QLabel[role="eta"][tone="error"] { color: #C05D54; }
QLabel[role="fileName"] { font-size: 14px; font-weight: 600; }
QFrame[role="drop"] { background: #F4F5FE; border: 1px dashed #9EBDB4; border-radius: 10px; }
QFrame[role="drop"][state="filled"] { background: white; border: 1px solid #C9D6F2; }
QFrame[role="drop"][state="active"] { background: #E1E3FF; border: 1px dashed #4548E8; }
QPushButton[role="choice"] { padding: 10px 12px; }
QPushButton[role="choice"]:checked { background: #EEEEFC; color: #373ADB; border-color: #4548E8; font-weight: 600; }
QLabel[role="studioTitle"] { color: #24273B; font-size: 24px; font-weight: 600; }
QLabel[role="muted"], QLabel[role="metricHint"], QLabel[role="metricLabel"], QLabel[role="statLabel"], QLabel[role="eyebrow"] { color: #62687D; font-size: 12px; }
QLabel[role="section"] { color: #24273B; font-size: 20px; }
QFrame[role="metric"] { background: transparent; border: none; border-left: 2px solid #DEE0F0; border-radius: 0; }
QFrame[role="card"] { border-radius: 12px; border-color: #D8E4DF; }
QPushButton[role="choice"] { padding: 10px 8px; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 2px solid #4548E8; padding: 8px 9px; }
QMainWindow, QWidget#workspace, QWidget#studioContent { background: #FCFCFF; }
QFrame#studioRail { background: #FCFCFF; border: none; border-bottom: 1px solid #EAECF5; }
QLabel[role="studioBrand"] { color: #24273B; font-size: 25px; font-weight: 600; padding: 0; }
QLabel[role="railCaption"] { color: #62687D; padding: 0; }
QFrame#studioRail QPushButton[role="nav"] { color: #62687D; background: transparent; text-align: center; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 12px 18px; }
QFrame#studioRail QPushButton[role="nav"]:checked { color: #4548E8; background: transparent; border-bottom: 2px solid #4548E8; }
QFrame#studioRail QPushButton[role="nav"]:hover { color: #4548E8; background: #F4F5FE; }
QFrame#studioRail QPushButton[role="nav"]:focus { border: 1px solid #4548E8; }
QLabel#studentPageTitle { color: #24273B; font-size: 39px; font-weight: 600; }
QLabel[role="atelierSubtitle"] { color: #73788E; font-size: 15px; }
QLabel[role="atelierHint"] { color: #73788E; font-size: 12px; padding-top: 18px; }
QFrame[role="atelierForm"], QFrame[role="atelierStage"] { background: transparent; border: none; }
QFrame[role="atelierReceipt"] { background: transparent; border: none; border-left: 1px solid #DFE2EF; }
QFrame[role="atelierDock"] { background: white; border: 1px solid #E1E4F2; border-radius: 14px; }
QLabel[role="price"] { color: #4548E8; font-size: 42px; }
QFrame[role="summary"] { background: transparent; border: none; }
QPushButton[role="choice"]:checked { background: #EEEEFF; color: #373ADB; border-color: #4548E8; }
QPushButton[role="primary"] { background: #4548E8; border-color: #4548E8; border-radius: 12px; padding: 13px 18px; }
QPushButton[role="primary"]:hover { background: #373ADB; border-color: #373ADB; }
QPushButton[role="primary"]:disabled { background: #C5C7EC; border-color: #C5C7EC; color: #FFFFFF; }
QPushButton[role="proofAction"] { color: #4548E8; background: #FFFFFF; border: 1px solid #E8E9F4; border-radius: 32px; padding: 0; font-size: 12px; }
QPushButton[role="proofAction"]:hover { color: #373ADB; border-color: #4548E8; background: #F9F9FF; }
QPushButton[role="proofAction"]:checked { color: white; background: #4548E8; border-color: #4548E8; }
QPushButton[role="proofAction"]:focus { border: 2px solid #4548E8; }
QPushButton[role="proofAction"]:disabled { color: #A1A5BD; background: #FAFAFE; border-color: #E8E9F4; }
)");
}

inline void chevron(QPainter& p, const QPointF& center, bool up = false) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#62687D"), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double direction = up ? -1 : 1;
    QPainterPath path;
    path.moveTo(center.x() - 3, center.y() - direction * 1.5);
    path.lineTo(center.x(), center.y() + direction * 1.5);
    path.lineTo(center.x() + 3, center.y() - direction * 1.5);
    p.drawPath(path);
}

class SelectInput : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        chevron(painter, QPointF(width() - 15, height() / 2.0));
    }
};

class NumberInput : public QSpinBox {
public:
    NumberInput() { setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); }
protected:
    void paintEvent(QPaintEvent* event) override {
        QSpinBox::paintEvent(event);
        QPainter painter(this);
        chevron(painter, QPointF(width() - 11, height() / 4.0 + 1), true);
        chevron(painter, QPointF(width() - 11, height() * 3 / 4.0 - 1));
    }
};

// Painted here because a style sheet cannot draw the tick without an image file.
class CheckBox : public QCheckBox {
public:
    explicit CheckBox(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }
    QSize sizeHint() const override { return {30 + fontMetrics().horizontalAdvance(text()), std::max(24, fontMetrics().height() + 8)}; }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    bool hitButton(const QPoint& position) const override { return rect().contains(position); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box(0.5, (height() - 18) / 2.0 + 0.5, 17, 17);
        const bool on = isChecked(), active = isEnabled();
        p.setPen(QPen(QColor(!active ? "#DCE1EA" : on ? "#4548E8" : hasFocus() || underMouse() ? "#4548E8" : "#C3CCDB"), 1.2));
        p.setBrush(QColor(!active ? (on ? "#C3D0EC" : "#F3F5F9") : on ? "#4548E8" : "#FFFFFF"));
        p.drawRoundedRect(box, 5, 5);
        if (on) {
            p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath tick;
            tick.moveTo(box.left() + 4.2, box.center().y() + 0.4);
            tick.lineTo(box.left() + 7.4, box.center().y() + 3.5);
            tick.lineTo(box.left() + 12.8, box.center().y() - 3.2);
            p.setBrush(Qt::NoBrush); p.drawPath(tick);
        }
        p.setPen(QColor(active ? "#293449" : "#A3ADBE"));
        p.drawText(QRect(26, 0, width() - 26, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};

inline QLabel* label(const QString& text, const char* role = nullptr) {
    auto* result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (role) result->setProperty("role", role);
    if (role && QLatin1String(role) == QLatin1String("badge")) {
        result->setFixedHeight(28);
        result->setAlignment(Qt::AlignCenter);
    }
    return result;
}

inline QFrame* card(const char* role = "card") {
    auto* result = new QFrame;
    result->setProperty("role", role);
    return result;
}

inline void restyle(QWidget* widget, const char* property, const char* value) {
    if (widget->property(property).toString() == QLatin1String(value)) return;
    widget->setProperty(property, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
inline void tone(QWidget* widget, const char* value) { restyle(widget, "tone", value); }

// Status line text with its colour: "ready" for a confirmed result, "error" for a problem.
inline void say(QLabel* target, const QString& text, const char* toneName = "") {
    target->setText(text);
    tone(target, toneName);
}

// Window identity in the header: a filled icon tile, the name and what this window is for.
inline QWidget* brand(Icon kind, const QString& title, const QString& subtitle) {
    auto* box = new QWidget;
    auto* row = new QHBoxLayout(box); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(10);
    auto* mark = new QLabel; mark->setObjectName("brandMark"); mark->setFixedSize(36, 36); mark->setAlignment(Qt::AlignCenter);
    mark->setPixmap(icon(kind, Qt::white).pixmap(22, 22)); row->addWidget(mark);
    auto* words = new QVBoxLayout; words->setSpacing(0); words->setContentsMargins(0, 0, 0, 0);
    auto* name = label(title); name->setObjectName("pageTitle"); words->addWidget(name);
    words->addWidget(label(subtitle, "eyebrow")); row->addLayout(words);
    return box;
}

struct Tint { QColor ink, fill; };
inline Tint tint(const QString& name) {
    if (name == QLatin1String("ready")) return {QColor("#267663"), QColor("#EAF5F1")};
    if (name == QLatin1String("waiting")) return {QColor("#805817"), QColor("#FAF2E4")};
    if (name == QLatin1String("error")) return {QColor("#A34239"), QColor("#FBEDEA")};
    if (name == QLatin1String("info")) return {QColor("#4548E8"), QColor("#EEEEFC")};
    return {QColor("#78869B"), QColor("#F0F3F8")};
}

// Cell roles understood by CellDelegate. The item text stays plain, so tooltips,
// accessibility and tests read the same words the delegate draws.
constexpr int ToneRole = Qt::UserRole + 20;     // ready / waiting / error / info / muted: draw the text as a pill
constexpr int RatioRole = Qt::UserRole + 21;    // 0..1: draw a progress track under the text
constexpr int EmphasisRole = Qt::UserRole + 22; // strong / accent / muted text

struct Cell {
    Cell(const QString& value, const char* toneName = "", double progress = -1, const char* weight = "")
        : text(value), tone(QLatin1String(toneName)), emphasis(QLatin1String(weight)), ratio(progress) {}
    QString text, tone, emphasis;
    double ratio;
};

class CellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillRect(option.rect, QColor(option.state.testFlag(QStyle::State_Selected) ? "#EEEEFC" : "#FFFFFF"));
        painter->setPen(QColor("#EDF0F5"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        const QRect area = option.rect.adjusted(10, 0, -10, 0);
        const auto value = index.data().toString();
        const auto toneName = index.data(ToneRole).toString();
        const auto emphasis = index.data(EmphasisRole).toString();
        painter->setFont(cellFont(option.font, emphasis, !toneName.isEmpty()));
        if (!toneName.isEmpty()) {
            const auto colors = tint(toneName);
            const int width = std::min(area.width(), painter->fontMetrics().horizontalAdvance(value) + 18);
            const QRect pill(area.left(), area.center().y() - 11, width, 24);
            painter->setPen(Qt::NoPen); painter->setBrush(colors.fill); painter->drawRoundedRect(pill, 6, 6);
            painter->setPen(colors.ink);
            painter->drawText(pill, Qt::AlignCenter, painter->fontMetrics().elidedText(value, Qt::ElideRight, width - 10));
        } else if (index.data(RatioRole).isValid()) {
            const double ratio = std::clamp(index.data(RatioRole).toDouble(), 0., 1.);
            painter->setPen(QColor("#59667C"));
            painter->drawText(QRect(area.left(), area.top() + 6, area.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(value, Qt::ElideRight, area.width()));
            const QRectF track(area.left(), area.bottom() - 11, std::min(area.width(), 120), 4);
            painter->setPen(Qt::NoPen); painter->setBrush(QColor("#E4E9F2")); painter->drawRoundedRect(track, 2, 2);
            if (ratio > 0) {
                painter->setBrush(QColor("#4548E8"));
                painter->drawRoundedRect(QRectF(track.left(), track.top(), std::max(4., track.width() * ratio), 4), 2, 2);
            }
        } else {
            painter->setPen(QColor(emphasis == QLatin1String("accent") ? "#373ADB" : emphasis == QLatin1String("strong") ? "#24273B"
                : emphasis == QLatin1String("muted") ? "#8A96A9" : "#3E4A60"));
            painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, painter->fontMetrics().elidedText(value, Qt::ElideRight, area.width()));
        }
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const bool pill = !index.data(ToneRole).toString().isEmpty();
        const QFontMetrics metrics(cellFont(option.font, index.data(EmphasisRole).toString(), pill));
        int width = metrics.horizontalAdvance(index.data().toString()) + 24 + (pill ? 18 : 0);
        if (index.data(RatioRole).isValid()) width = std::max(width, 120);
        return {width, 42};
    }
private:
    static QFont cellFont(QFont font, const QString& emphasis, bool pill) {
        font.setPixelSize(pill ? 12 : 13);
        if (emphasis == QLatin1String("strong") || emphasis == QLatin1String("accent")) font.setWeight(QFont::DemiBold);
        return font;
    }
};

// A read-only list with the shared row height, header and cell painting.
inline QTableWidget* listTable(const char* name, const QStringList& headers) {
    auto* view = new QTableWidget(0, static_cast<int>(headers.size()));
    view->setObjectName(name);
    view->setHorizontalHeaderLabels(headers);
    view->setItemDelegate(new CellDelegate(view));
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view->horizontalHeader()->setHighlightSections(false);
    view->verticalHeader()->hide();
    view->verticalHeader()->setDefaultSectionSize(42);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setShowGrid(false);
    view->setWordWrap(false);
    return view;
}
inline void setCell(QTableWidget* view, int row, int column, const Cell& cell) {
    auto* item = view->item(row, column);
    if (!item) { item = new QTableWidgetItem; view->setItem(row, column, item); }
    if (item->text() != cell.text) { item->setText(cell.text); item->setToolTip(cell.text); }
    if (item->data(ToneRole).toString() != cell.tone) item->setData(ToneRole, cell.tone);
    if (item->data(EmphasisRole).toString() != cell.emphasis) item->setData(EmphasisRole, cell.emphasis);
    const QVariant ratio = cell.ratio < 0 ? QVariant() : QVariant(cell.ratio);
    if (item->data(RatioRole) != ratio) item->setData(RatioRole, ratio);
}
// Every row stays visible; the surrounding page scrolls instead of the list.
inline void fitRows(QTableWidget* view) {
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFixedHeight(view->horizontalHeader()->sizeHint().height() + view->rowCount() * view->verticalHeader()->defaultSectionSize() + 4);
}

// Centred message over an empty list. Mouse events pass through to the view.
inline QWidget* emptyState(QAbstractItemView* view, const QString& title, const QString& hint) {
    auto* overlay = new QVBoxLayout(view->viewport()); overlay->setContentsMargins(0, 0, 0, 0);
    auto* box = new QWidget; box->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* lines = new QVBoxLayout(box); lines->setSpacing(6); lines->addStretch();
    auto* heading = label(title, "cardTitle"); heading->setAlignment(Qt::AlignCenter); lines->addWidget(heading);
    auto* detail = label(hint, "muted"); detail->setAlignment(Qt::AlignCenter); lines->addWidget(detail);
    lines->addStretch(); overlay->addWidget(box);
    return box;
}

// Non-blocking confirmation for an action that cannot be undone. The safe button is the default.
inline QDialog* confirm(QWidget* parent, const char* name, const QString& title, const QString& body,
                        const QString& action, std::function<void()> accepted) {
    auto* dialog = new QDialog(parent); dialog->setObjectName(name); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title); dialog->setWindowModality(Qt::WindowModal); dialog->setFixedWidth(420);
    auto* layout = new QVBoxLayout(dialog); layout->setContentsMargins(28, 24, 28, 22); layout->setSpacing(12);
    layout->addWidget(label(title, "section"));
    // A minimum width makes the wrapped label report the height it needs at the dialog's width.
    auto* detail = label(body, "body"); detail->setWordWrap(true); detail->setMinimumWidth(420 - 2 * 28); layout->addWidget(detail);
    layout->addSpacing(6);
    auto* buttons = new QHBoxLayout; buttons->setSpacing(10);
    auto* back = new QPushButton(QStringLiteral("返回")); back->setObjectName("confirmBack"); back->setDefault(true);
    auto* proceed = new QPushButton(action); proceed->setObjectName("confirmAction"); proceed->setProperty("role", "dangerPrimary");
    proceed->setAutoDefault(false);
    buttons->addWidget(back, 1); buttons->addWidget(proceed, 1); layout->addLayout(buttons);
    QObject::connect(back, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(proceed, &QPushButton::clicked, dialog, &QDialog::accept);
    QObject::connect(dialog, &QDialog::accepted, parent, std::move(accepted));
    dialog->open();
    return dialog;
}

// Shared by the owner console and the student client so both render Chinese text
// with an installed CJK family instead of fallback boxes.
inline void applyTheme(QWidget& window) {
    QFont interfaceFont = window.font();
    const auto families = QFontDatabase::families();
    const QStringList candidates{
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei")
    };
    for (const auto& family : candidates) {
        if (!families.contains(family)) continue;
        QFont candidate = interfaceFont;
        candidate.setFamily(family);
        if (!QFontMetrics(candidate).inFontUcs4(0x6821)) continue;
        interfaceFont = candidate;
        break;
    }
    // Qt's native style derives point-sized fonts for menus and title controls.
    // Keep a valid point size here; widget text uses the pixel scale in QSS.
    interfaceFont.setPointSizeF(10);
    QApplication::setFont(interfaceFont);
    window.setFont(interfaceFont);
    QPalette light = window.palette();
    light.setColor(QPalette::Window, QColor("#FAFBFF"));
    light.setColor(QPalette::WindowText, QColor("#24273B"));
    light.setColor(QPalette::Base, Qt::white); light.setColor(QPalette::Text, QColor("#293449"));
    light.setColor(QPalette::Button, Qt::white); light.setColor(QPalette::ButtonText, QColor("#59667C"));
    light.setColor(QPalette::Highlight, QColor("#E1E3FF")); light.setColor(QPalette::HighlightedText, QColor("#234B9D"));
    window.setPalette(light);
    window.setStyleSheet(styleSheet());
}
} // namespace cps::gui::ui
