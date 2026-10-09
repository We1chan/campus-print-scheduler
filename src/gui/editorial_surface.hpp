#pragma once
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVector>
#include <QSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QShowEvent>

namespace cps::gui {
class EditorialToolButton : public QPushButton {
  public:
    using QPushButton::QPushButton;

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(width() / 61., height() / 64.);
        p.fillRect(QRectF(0, 0, 61, 64), isDown() || isChecked() ? QColor(255, 245, 239, 245) : QColor(255, 255, 255, 238));
        p.setOpacity(isEnabled() ? 1. : .4);
        icon().paint(&p, QRect(21, 7, 20, 20));
        QFont f("Microsoft YaHei");
        f.setPixelSize(13);
        p.setFont(f);
        p.setPen(QColor("#14213E"));
        p.drawText(QRect(0, 30, 61, 33), Qt::AlignCenter, text().section('\n', -1));
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QColor("#D63D29"));
            p.drawRect(QRectF(1, 1, 59, 62));
        }
    }
};
class EditorialServiceButton : public QPushButton {
  public:
    using QPushButton::QPushButton;

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal scale = width() / 304.;
        p.scale(scale, height() / 64.);
        const QColor active("#D63D29"), ink("#14213E");
        p.setPen(QPen(isChecked() ? active : QColor("#8C93A1"), 1.4));
        p.setBrush(Qt::white);
        p.drawEllipse(QRectF(4, 22, 24, 24));
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawEllipse(QRectF(8, 26, 16, 16));
        }
        QFont f("Microsoft YaHei");
        f.setPixelSize(18);
        f.setBold(true);
        p.setFont(f);
        p.setPen(ink);
        p.drawText(QRectF(46, 12, 55, 43), Qt::AlignVCenter, property("caption").toString());
        f.setBold(false);
        f.setPixelSize(14);
        p.setFont(f);
        p.setPen(QColor("#7C8494"));
        p.drawText(QRectF(103, 12, 114, 43), Qt::AlignVCenter, property("window").toString());
        f.setPixelSize(18);
        f.setBold(true);
        p.setFont(f);
        p.setPen(ink);
        p.drawText(QRectF(221, 12, 80, 43), Qt::AlignRight | Qt::AlignVCenter, property("price").toString());
        if (hasFocus()) {
            p.setPen(QPen(active, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(1, 1, 302, 62), 4, 4);
        }
    }
};
class EditorialNumberInput : public QSpinBox {
  public:
    EditorialNumberInput() {
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignCenter);
        lineEdit()->setFrame(false);
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QSpinBox::resizeEvent(event);
        lineEdit()->setGeometry(width() / 4, 2, width() / 2, height() - 4);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(hasFocus() ? QColor("#D63D29") : QColor("#D9DCE2"), 1));
        p.setBrush(Qt::white);
        p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
        p.setPen(QColor("#D9DCE2"));
        p.drawLine(width() / 4, 1, width() / 4, height() - 1);
        p.drawLine(width() * 3 / 4, 1, width() * 3 / 4, height() - 1);
        p.setPen(QColor("#69748A"));
        QFont f = font();
        f.setPixelSize(std::max(14, height() / 2));
        p.setFont(f);
        p.drawText(QRect(0, 0, width() / 4, height()), Qt::AlignCenter, QStringLiteral("−"));
        p.drawText(QRect(width() * 3 / 4, 0, width() / 4, height()), Qt::AlignCenter, "+");
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->position().x() < width() / 4) {
            stepDown();
            setFocus();
        } else if (event->position().x() > width() * 3 / 4) {
            stepUp();
            setFocus();
        } else
            QSpinBox::mousePressEvent(event);
    }
};
class EditorialSurface : public QFrame {
  public:
    explicit EditorialSurface(QWidget *parent = nullptr) : QFrame(parent) {
        setObjectName("editorialSurface");
    }
    void setReferenceHeight(qreal height) {
        referenceHeight_ = height;
        shell_ = false;
        position();
    }
    void place(QWidget *widget, QRectF box, int fontSize = 16, const QString &style = {}) {
        widget->setParent(this);
        items_.append({widget, box, fontSize, style});
        widget->show();
        position();
    }
    void move(QWidget *widget, QRectF box) {
        for (auto &item : items_)
            if (item.widget == widget)
                item.box = box;
        position();
    }
    void setNavigation(int index) {
        nav_ = index;
        update();
    }

  protected:
    void showEvent(QShowEvent *event) override {
        QFrame::showEvent(event);
        position();
    }
    void resizeEvent(QResizeEvent *e) override {
        QFrame::resizeEvent(e);
        position();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.scale(width() / 1586., height() / referenceHeight_);
        p.fillRect(QRectF(0, 0, 1586, 992), QColor("#FAFAF9"));
        p.fillRect(QRectF(1219, 55, 367, 787), QColor("#FAFAF9"));
        p.setPen(QColor(30, 35, 48, 4));
        for (int y = 0; y < 992; y += 9)
            for (int x = 0; x < 1586; x += 11) {
                const unsigned seed = unsigned(x * 101 + y * 173);
                p.drawPoint(x + seed % 9, y + (seed / 17) % 7);
            }
        if (!shell_)
            return;
        p.setPen(QColor("#DADDE2"));
        p.drawLine(0, 55, 1586, 55);
        if (nav_ == 0)
            p.drawLine(1219, 55, 1219, 842);
        p.drawLine(0, 842, 1586, 842);
        p.drawLine(36, 941, 1550, 941);
        p.fillRect(QRectF(620 + nav_ * 130, 51, 85, 4), QColor("#D63D29"));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#D6402C"));
        p.drawPolygon(QPolygonF{{38, 22}, {57, 11}, {77, 17}, {57, 30}});
        p.setBrush(QColor("#122B47"));
        p.drawPolygon(QPolygonF{{38, 32}, {57, 23}, {77, 30}, {57, 43}});
        p.setPen(QPen(QColor("#14213E"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(53, 880, 24, 29), 2, 2);
        p.drawLine(49, 884, 49, 912);
        p.drawLine(46, 888, 46, 915);
        p.drawEllipse(QRectF(493, 880, 29, 29));
        p.drawLine(508, 884, 508, 895);
        p.drawLine(508, 895, 516, 899);
        p.setPen(QColor("#DADDE2"));
        p.drawLine(446, 873, 446, 912);
    }

  private:
    struct Item {
        QWidget *widget;
        QRectF box;
        int fontSize;
        QString style;
    };
    QVector<Item> items_;
    int nav_ = 0;
    qreal referenceHeight_ = 992;
    bool shell_ = true;
    void position() {
        const qreal sx = width() / 1586., sy = height() / referenceHeight_;
        const qreal scale = std::min(sx, sy);
        for (const auto &item : items_) {
            if (item.fontSize > 0)
                item.widget->setStyleSheet(
                    item.style + (item.style.isEmpty() ? QString() : QStringLiteral(";")) +
                    QStringLiteral("font-size:%1px;").arg(std::max(10, qRound(item.fontSize * scale))));
            item.widget->ensurePolished();
            item.widget->setGeometry(qRound(item.box.x() * sx), qRound(item.box.y() * sy),
                                     qRound(item.box.width() * sx), qRound(item.box.height() * sy));
            // Resolve the inherited font before the first native-style paint.
            // Also keep labels available when a compact button cannot fit them.
            if (auto *button = qobject_cast<QPushButton *>(item.widget)) {
                const QSize textSize = QFontMetrics(button->font()).size(Qt::TextShowMnemonic, button->text());
                if (textSize.width() > button->width() && button->toolTip().isEmpty())
                    button->setToolTip(button->text());
            }
            if (auto *label = qobject_cast<QLabel *>(item.widget); label && label->property("fullText").isValid())
                label->setText(QFontMetrics(label->font()).elidedText(
                    label->property("fullText").toString(), Qt::ElideMiddle, label->width()));
        }
    }
};
inline QLabel *editorialLabel(const QString &text, int size = 16, bool bold = false) {
    auto *label = new QLabel(text);
    QFont f("Microsoft YaHei");
    f.setPixelSize(size);
    f.setBold(bold);
    label->setFont(f);
    label->setStyleSheet("color:#14213E;background:transparent;");
    return label;
}
} // namespace cps::gui
