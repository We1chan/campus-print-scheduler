#pragma once
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>

namespace cps::gui {
// One translucent surface over the desk, with independent keyboard-focusable choices.
class EditorialColorSwitch : public QFrame {
public:
    EditorialColorSwitch() {
        setObjectName("colorSwitch");
        setStyleSheet("QFrame#colorSwitch{background:transparent;border:none;}");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(9, 7, 9, 9);
        row->setSpacing(2);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF surface = QRectF(rect()).adjusted(4, 2, -4, -6);
        p.setPen(Qt::NoPen);
        for (int i = 4; i > 0; --i) {
            p.setBrush(QColor(38, 32, 24, 5));
            p.drawRoundedRect(surface.adjusted(-i, -i * .3, i, i).translated(0, 2), 16, 16);
        }
        QLinearGradient glass(0, 0, 0, height());
        glass.setColorAt(0, QColor(255, 255, 253, 222));
        glass.setColorAt(1, QColor(247, 244, 236, 185));
        p.setBrush(glass);
        p.setPen(QPen(QColor(255, 255, 255, 220), 1));
        p.drawRoundedRect(surface, 16, 16);
    }
};
class EditorialColorButton : public QPushButton {
public:
    explicit EditorialColorButton(const QString &text) : QPushButton(text) {
        setCursor(Qt::PointingHandCursor);
        setCheckable(true);
        setAccessibleName(text);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setStyleSheet("background:transparent;border:none;");
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor ink(isChecked() ? "#C83D2B" : "#24314A");
        p.setPen(hasFocus() ? QPen(QColor("#D63D29"), 1.2) : QPen(Qt::NoPen));
        p.setBrush(isChecked() ? QColor(255, 249, 242, 225)
                              : underMouse() ? QColor(255, 255, 255, 140) : QColor(Qt::transparent));
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 11, 11);
        QFont f("Microsoft YaHei");
        f.setPixelSize(qBound(12, height() * 17 / 46, 19));
        f.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
        p.setFont(f);
        const qreal total = p.fontMetrics().horizontalAdvance(text()) + 17;
        const qreal left = (width() - total) / 2;
        p.setBrush(ink);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(left + 3, height() / 2.), 3, 3);
        p.setPen(ink);
        p.drawText(QRectF(left + 14, 0, total - 14, height()), Qt::AlignVCenter, text());
    }
};
} // namespace cps::gui
