#pragma once
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QPushButton>
#include <QStyleOptionButton>

namespace cps::gui {
// Local vector marks remain crisp at native Windows display scales.
inline QIcon paymentIcon(const QString &method) {
    QPixmap canvas(72, 72);
    canvas.setDevicePixelRatio(3);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (method == QLatin1String("wechat")) {
        const QColor green("#07B75A");
        p.setBrush(green);
        p.drawEllipse(QRectF(1, 3, 16, 13));
        QPainterPath tail;
        tail.moveTo(4, 13); tail.lineTo(3, 18); tail.lineTo(9, 14); tail.closeSubpath();
        p.drawPath(tail);
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(5, 7, 1.8, 1.8)); p.drawEllipse(QRectF(11, 7, 1.8, 1.8));
        // White separation makes the two overlapping chat bubbles legible.
        p.setPen(QPen(Qt::white, 1.2)); p.setBrush(green);
        p.drawEllipse(QRectF(9, 10, 14, 11));
        p.setPen(Qt::NoPen);
        QPainterPath secondTail;
        secondTail.moveTo(17, 19); secondTail.lineTo(21, 23); secondTail.lineTo(21, 18); secondTail.closeSubpath();
        p.drawPath(secondTail);
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(12.5, 13.5, 1.5, 1.5)); p.drawEllipse(QRectF(18, 13.5, 1.5, 1.5));
    } else if (method == QLatin1String("alipay")) {
        p.setBrush(QColor("#1677FF"));
        p.drawRoundedRect(QRectF(2, 2, 20, 20), 4, 4);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(12, 5), QPointF(12, 10));
        p.drawLine(QPointF(6, 8), QPointF(18, 8));
        QPainterPath mark;
        mark.moveTo(6, 11); mark.lineTo(17, 11);
        mark.cubicTo(15, 16, 11, 18, 6, 19);
        mark.moveTo(7, 12); mark.cubicTo(9, 17, 16, 19, 23, 19);
        p.drawPath(mark);
    } else {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#14213E"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(QRectF(2, 4, 20, 16), 2, 2);
        p.drawLine(QPointF(2, 9), QPointF(22, 9));
        p.drawLine(QPointF(6, 15), QPointF(11, 15));
    }
    return QIcon(canvas);
}

class PaymentMethodButton : public QPushButton {
  public:
    using QPushButton::QPushButton;
  protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text.clear();
        option.icon = {};
        QPainter p(this);
        style()->drawControl(QStyle::CE_PushButton, &option, &p, this);
        icon().paint(&p, QRect(width() / 2 - 12, 11, 24, 24), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
        p.setFont(font());
        p.setPen(!isEnabled() ? QColor("#979DA8") : isChecked() ? QColor("#D63D29") : QColor("#14213E"));
        p.drawText(QRect(5, 40, width() - 10, height() - 46), Qt::AlignCenter, text());
    }
};
} // namespace cps::gui
