#pragma once
#include <QAbstractButton>
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QWindow>

namespace cps::gui {
inline QString editorialDialogStyle() {
    return QStringLiteral(R"(
QDialog { background:#FAFAF9; color:#14213E; }
QWidget { font-size:14px; }
QFrame#editorialDialogHeader { background:#FAFAF9; border:none; border-bottom:1px solid #DADDE2; }
QLabel { color:#14213E; background:transparent; border:none; }
QLabel[role="muted"], QLabel[role="body"] { color:#69748A; font-size:13px; }
QLabel[role="section"] { color:#14213E; font-size:21px; font-weight:600; }
QLabel[role="price"] { color:#14213E; font-size:44px; font-weight:700; }
QLabel[tone="error"] { color:#B63D2D; }
QLabel[tone="ready"] { color:#B93424; }
QFrame[role="summary"] { background:transparent; border:none; border-top:1px solid #DADDE2; border-bottom:1px solid #DADDE2; border-radius:0; }
QPushButton { color:#14213E; background:white; border:1px solid #DADDE2; border-radius:6px; padding:9px 14px; }
QPushButton:hover { background:#FFF6F2; border-color:#D63D29; }
QPushButton:focus { border:2px solid #D63D29; }
QPushButton:pressed { background:#F7E9E3; }
QPushButton:checked { color:#D63D29; background:#FFF6F2; border-color:#D63D29; }
QPushButton:disabled { color:#979DA8; background:#F1F0ED; border-color:#E2E0DB; }
QPushButton[role="primary"] { color:white; background:#D63D29; border-color:#D63D29; font-weight:600; }
QPushButton[role="primary"]:hover { background:#BF3322; border-color:#BF3322; }
QPushButton[role="primary"]:pressed { background:#A72C1E; }
QPushButton[role="primary"]:disabled { color:#FFF7F4; background:#CFA097; border-color:#CFA097; }
QScrollArea { background:#EAE7E1; border:none; }
QScrollBar:vertical { background:transparent; width:9px; margin:8px 1px; }
QScrollBar:horizontal { background:transparent; height:9px; margin:1px 8px; }
QScrollBar::handle { background:#B5B2AB; border-radius:4px; min-height:32px; min-width:32px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
QScrollBar::add-page, QScrollBar::sub-page { background:transparent; }
QToolTip { color:white; background:#14213E; border:none; padding:6px 9px; }
)");
}

// Shared owner window and dialog controls.
// Widgets keep native focus/accessibility; the system handles drag and resize.
class EditorialChrome : public QObject, public QAbstractNativeEventFilter {
  public:
    EditorialChrome(QWidget *window, qreal titleHeight, qreal referenceHeight = 0, bool resizable = true);
    ~EditorialChrome() override;
    void toggleMaximized() {
        if (window_->isMaximized()) window_->showNormal();
        else window_->showMaximized();
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override;
  protected:
    bool eventFilter(QObject *object, QEvent *event) override;
  private:
    void updateSystemFrame(bool active);
    bool draggable(QPoint point) const {
        const qreal band = referenceHeight_ ? titleHeight_ * window_->height() / referenceHeight_ : titleHeight_;
        if (point.y() < 0 || point.y() >= band) return false;
        for (auto *child = window_->childAt(point); child && child != window_; child = child->parentWidget())
            if (qobject_cast<QAbstractButton *>(child) || qobject_cast<QLineEdit *>(child)) return false;
        return true;
    }
    QPointer<QWidget> window_;
    qreal titleHeight_, referenceHeight_;
    bool resizable_;
};

class EditorialWindowButton : public QPushButton {
  public:
    enum Action { Minimize, Maximize, Close };
    EditorialWindowButton(QWidget *window, Action action, const QString &id) : action_(action), window_(window) {
        setObjectName(id);
        setAccessibleName(action == Minimize ? QStringLiteral("最小化") : action == Maximize ? QStringLiteral("最大化 / 还原") : QStringLiteral("关闭"));
        setToolTip(accessibleName());
        setAutoDefault(false);
        setCursor(Qt::ArrowCursor);
        setMinimumSize(28, 28);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(this, &QPushButton::clicked, window, [this] {
            if (action_ == Minimize) window_->showMinimized();
            else if (action_ == Maximize) {
                if (window_->isMaximized()) window_->showNormal(); else window_->showMaximized();
            } else window_->close();
        });
    }
    QSize sizeHint() const override { return {44, 48}; }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool active = underMouse() || isDown();
        if (active) p.fillRect(rect(), action_ == Close ? QColor("#D63D29") : QColor("#EDEBE7"));
        p.setPen(QPen(!isEnabled() ? QColor("#A2A7B0") : active && action_ == Close ? Qt::white : QColor("#14213E"), 1.4));
        const QPointF c(width() / 2., height() / 2.);
        if (action_ == Minimize) p.drawLine(c + QPointF(-6, 3), c + QPointF(6, 3));
        else if (action_ == Close) {
            p.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5));
            p.drawLine(c + QPointF(-5, 5), c + QPointF(5, -5));
        } else {
            p.setBrush(Qt::NoBrush);
            if (window_->isMaximized()) {
                p.drawRect(QRectF(c.x() - 3, c.y() - 6, 10, 10));
                p.fillRect(QRectF(c.x() - 6, c.y() - 3, 10, 10), active ? QColor("#EDEBE7") : QColor("#FAFAF9"));
            }
            p.drawRect(QRectF(c.x() - 6, c.y() - 3, 10, 10));
        }
        if (hasFocus()) { p.setBrush(Qt::NoBrush); p.setPen(QColor("#D63D29")); p.drawRect(rect().adjusted(2, 2, -3, -3)); }
    }
  private:
    Action action_;
    QPointer<QWidget> window_;
};

class EditorialElidedLabel : public QLabel {
  public:
    explicit EditorialElidedLabel(const QString &text) : fullText_(text) {
        setToolTip(text);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
  protected:
    void resizeEvent(QResizeEvent *event) override {
        QLabel::resizeEvent(event);
        setText(QFontMetrics(font()).elidedText(fullText_, Qt::ElideMiddle, width()));
    }
  private:
    QString fullText_;
};

class EditorialDialog : public QDialog {
  public:
    explicit EditorialDialog(QWidget *parent, bool resizable = true) : QDialog(parent) {
        setStyleSheet(editorialDialogStyle());
        new EditorialChrome(this, 64, 0, resizable);
    }
    void reject() override { if (!property("busy").toBool()) QDialog::reject(); }
  protected:
    void closeEvent(QCloseEvent *event) override {
        if (property("busy").toBool()) event->ignore(); else QDialog::closeEvent(event);
    }
};

inline QFrame *editorialDialogHeader(QDialog *dialog, const QString &title, const QString &file = {}, bool controls = true) {
    auto *header = new QFrame;
    header->setObjectName("editorialDialogHeader");
    header->setFixedHeight(64);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(24, 0, 4, 0);
    row->setSpacing(12);
    auto *mark = new QLabel;
    mark->setPixmap(dialog->windowIcon().pixmap(32, 32));
    row->addWidget(mark);
    auto *caption = new QLabel(title);
    caption->setStyleSheet("color:#14213E;font-size:17px;font-weight:600;");
    row->addWidget(caption);
    if (!file.isEmpty()) {
        auto *name = new EditorialElidedLabel(file);
        name->setStyleSheet("color:#69748A;font-size:12px;");
        row->addWidget(name, 1);
    } else row->addStretch();
    if (controls) {
        row->addWidget(new EditorialWindowButton(dialog, EditorialWindowButton::Minimize, "dialogMinimize"));
        row->addWidget(new EditorialWindowButton(dialog, EditorialWindowButton::Maximize, "dialogMaximize"));
    }
    row->addWidget(new EditorialWindowButton(dialog, EditorialWindowButton::Close, "dialogClose"));
    return header;
}
} // namespace cps::gui
