#pragma once
#include "ui_theme.hpp"
#include <QMouseEvent>
#include <QPolygonF>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <cmath>

namespace cps::gui {
// The live paper scene is rendered with native vectors, independently of the concept image.
class PrintProof : public QWidget {
public:
    explicit PrintProof(QWidget* parent = nullptr) : QWidget(parent), turning_(this), spreading_(this), sizing_(this) {
        setObjectName("printProof"); setMinimumSize(260, 350);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); setMouseTracking(true);
        bind(turning_, 440, turn_); bind(spreading_, 480, spread_); bind(sizing_, 260, size_); describe();
    }
    void setSpecification(const QString& title, int pages, int copies, bool a3, bool color, bool duplex, bool chosen) {
        if (a3 != a3_) animate(sizing_, size_, a3 ? 1.07 : 1.);
        title_ = title; pages_ = pages; copies_ = copies; a3_ = a3; color_ = color; duplex_ = duplex; chosen_ = chosen;
        sheet_ = std::min(sheet_, sheetCount() - 1);
        if (!duplex_) setBack(false);
        describe(); update(); if (changed) changed();
    }
    void setBack(bool back) {
        const bool next = back && duplex_;
        if (next == back_) return;
        back_ = next; animate(turning_, turn_, back_ ? 1. : 0.); describe(); if (changed) changed();
    }
    void setExpanded(bool expanded) {
        expanded_ = expanded; animate(spreading_, spread_, expanded ? 1. : 0.); describe(); if (changed) changed();
    }
    void stepPage(int direction) {
        sheet_ = std::clamp(sheet_ + direction, 0, sheetCount() - 1); describe(); update(); if (changed) changed();
    }
    int currentSheet() const { return sheet_ + 1; }
    int sheetCount() const { return duplex_ ? (pages_ + 1) / 2 : pages_; }
    bool showingBack() const { return back_; }
    bool expanded() const { return expanded_; }
    bool motionEnabled() const { return motion_; }
    void setMotion(bool enabled) {
        motion_ = enabled;
        if (!enabled) {
            turning_.stop(); spreading_.stop(); sizing_.stop();
            turn_ = back_ ? 1. : 0.; spread_ = expanded_ ? 1. : 0.; size_ = a3_ ? 1.07 : 1.; pointer_ = {}; update();
        }
    }
    QString specification() const { return accessibleDescription(); }
    void attachActions(QPushButton* flip, QPushButton* unfold) {
        flip_ = flip; unfold_ = unfold;
        for (auto* action : {flip_, unfold_}) { action->setParent(this); action->setProperty("role", "proofAction"); action->show(); }
        positionActions();
    }
    std::function<void()> changed;
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); positionActions(); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (motion_) {
            pointer_ = {(event->position().x() / width() - .5) * 2., (event->position().y() / height() - .5) * 2.}; update();
        }
    }
    void leaveEvent(QEvent*) override { pointer_ = {}; update(); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient desk(0, 0, width(), height());
        desk.setColorAt(0, QColor("#FAFAFF")); desk.setColorAt(.65, QColor("#F0F1FB")); desk.setColorAt(1, QColor("#F7F8FF"));
        p.fillRect(rect(), desk);
        const qreal scale = std::min(width() / 380., (height() - 40) / 370.) * size_;
        p.save(); p.translate(width() * .51 + pointer_.x() * 6, height() * .45 + pointer_.y() * 3); p.scale(scale, scale);
        p.setPen(Qt::NoPen);
        for (int i = 14; i > 0; --i) {
            p.setBrush(QColor(45, 50, 108, 3)); p.drawEllipse(QRectF(-150 - i, 130 - i * .5, 292 + i * 2, 50 + i));
        }
        const int layers = std::min(7, sheetCount() * std::min(copies_, 3));
        for (int i = layers - 1; i >= 0; --i) {
            p.save(); p.translate(i * 2.7 + spread_ * i * 13, i * 4.2 - spread_ * i * 10); p.rotate(-5. + pointer_.x() * 1.5 + spread_ * i * 4);
            const QPolygonF target{QPointF(-70, -157), QPointF(139, -133), QPointF(77, 151), QPointF(-152, 119)};
            QTransform perspective;
            QTransform::quadToQuad(QPolygonF{QPointF(0, 0), QPointF(220, 0), QPointF(220, 310), QPointF(0, 310)}, target, perspective);
            p.setTransform(perspective, true); p.setPen(QPen(QColor("#D5D8E7"), .7));
            QLinearGradient paper(0, 0, 200, 310); paper.setColorAt(0, Qt::white); paper.setColorAt(1, QColor(i ? "#F5F5FD" : "#FFFFFF"));
            p.setBrush(paper); p.drawRect(QRectF(0, 0, 220, 310));
            if (i == 0) {
                p.save(); const qreal flip = std::max(.04, std::abs(std::cos(turn_ * 3.141592653589793)));
                p.translate(110, 0); p.scale(flip, 1); p.translate(-110, 0); p.fillRect(QRectF(0, 0, 220, 310), Qt::white);
                drawSheet(p, turn_ > .5); p.restore();
            } else {
                p.setPen(Qt::NoPen); p.setBrush(color_ ? QColor("#4548E8") : QColor("#6A6D7D")); p.drawRect(QRectF(180, 256, 11, 28));
            }
            p.restore();
        }
        p.restore(); QFont f = font(); f.setPixelSize(12); p.setFont(f); p.setPen(QColor("#60667D"));
        const int page = duplex_ ? sheet_ * 2 + (back_ ? 2 : 1) : sheet_ + 1;
        p.drawText(QRect(12, height() - 37, width() - 24, 22), Qt::AlignCenter,
            QStringLiteral("第 %1 / %2 张 · %3 · %4").arg(sheet_ + 1).arg(sheetCount())
                .arg(back_ ? QStringLiteral("背面") : QStringLiteral("正面"), page > pages_ ? QStringLiteral("留白") : QStringLiteral("第 %1 页").arg(page)));
    }
private:
    void positionActions() {
        if (!flip_ || !unfold_) return;
        const qreal scale = std::min(width() / 380., (height() - 40) / 370.);
        const int x = std::min(width() - 72, int(width() * .51 + scale * 152));
        flip_->setGeometry(x, int(height() * .24), 64, 64);
        unfold_->setGeometry(x, int(height() * .24) + 78, 64, 64);
    }
    void bind(QVariantAnimation& animation, int duration, qreal& value) {
        animation.setDuration(duration); animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation, &QVariantAnimation::valueChanged, this, [this, &value](const QVariant& next) { value = next.toReal(); update(); });
    }
    void animate(QVariantAnimation& animation, qreal& value, qreal target) {
        animation.stop(); if (motion_) { animation.setStartValue(value); animation.setEndValue(target); animation.start(); }
        else { value = target; update(); }
    }
    static void text(QPainter& p, QRectF box, const QString& value, int size, QColor ink, bool bold = false) {
        QFont f = p.font(); f.setPixelSize(size); f.setBold(bold); p.setFont(f); p.setPen(ink);
        p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(value, Qt::ElideRight, int(box.width())));
    }
    void drawSheet(QPainter& p, bool rear) {
        const QColor ink("#24273B"), muted("#73788E");
        const QColor primary = color_ ? QColor("#4548E8") : QColor("#505567");
        const QColor cyan = color_ ? QColor("#45D0E5") : QColor("#B0B5C3");
        p.setPen(Qt::NoPen); p.setBrush(primary); p.drawPolygon(QPolygonF{QPointF(18, 18), QPointF(31, 11), QPointF(30, 28)});
        p.setBrush(cyan); p.drawPolygon(QPolygonF{QPointF(18, 18), QPointF(18, 30), QPointF(30, 28)});
        text(p, {36, 12, 140, 20}, QStringLiteral("纸间"), 12, ink, true);
        const int page = duplex_ ? sheet_ * 2 + (rear ? 2 : 1) : sheet_ + 1;
        text(p, {178, 13, 32, 18}, QStringLiteral("%1/%2").arg(std::min(page, pages_)).arg(pages_), 8, muted);
        text(p, {18, 42, 185, 25}, QStringLiteral("印前规格示意"), 17, ink, true);
        text(p, {18, 70, 185, 16}, QStringLiteral("仅为规格示意，非实际文件内容"), 8, muted);
        if (page > pages_) {
            text(p, {35, 144, 160, 25}, QStringLiteral("这一面，留白。"), 17, muted, true);
            text(p, {32, 180, 160, 24}, QStringLiteral("奇数页文档的最后一张纸"), 9, muted);
        } else {
            p.setPen(Qt::NoPen); p.setBrush(primary); p.drawEllipse(QRectF(35, 106, 115, 115));
            p.setBrush(Qt::white); p.drawPolygon(QPolygonF{QPointF(99, 164), QPointF(149, 127), QPointF(153, 223)});
            QLinearGradient blocks(87, 184, 184, 264); blocks.setColorAt(0, QColor("#F1F3FC")); blocks.setColorAt(1, QColor("#A5ACCE"));
            p.setBrush(blocks); p.drawPolygon(QPolygonF{QPointF(61, 231), QPointF(107, 190), QPointF(158, 200), QPointF(119, 266)});
            p.setBrush(cyan); p.drawPolygon(QPolygonF{QPointF(119, 198), QPointF(172, 169), QPointF(201, 232), QPointF(148, 235)});
            p.setBrush(ink); p.drawPolygon(QPolygonF{QPointF(88, 234), QPointF(109, 215), QPointF(111, 267), QPointF(88, 273)});
            p.setBrush(primary); p.drawRect(QRectF(177, 113, 24, 24)); p.setPen(QPen(QColor("#D0D4E9"), .55));
            for (int i = 0; i < 6; ++i) p.drawEllipse(QRectF(59 + i * 3, 195 + i * 2, 86, 39));
            for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) p.drawPoint(QPointF(173 + x * 5, 250 + y * 5));
        }
        text(p, {18, 282, 150, 16}, chosen_ ? title_ : QStringLiteral("你的下一份作品"), 9, ink);
        text(p, {177, 280, 32, 20}, a3_ ? "A3" : "A4", 16, primary, true);
        QPainterPath fold; fold.moveTo(198, 310); fold.quadTo(209, 300, 206, 286); fold.lineTo(220, 286); fold.quadTo(220, 303, 198, 310);
        p.setPen(QPen(QColor("#E1E4F0"), .5)); p.setBrush(QColor("#F2F3FA")); p.drawPath(fold);
    }
    void describe() {
        setAccessibleName(QStringLiteral("打印规格示意"));
        setAccessibleDescription(QStringLiteral("%1，%2 页，%3 份，%4，%5，%6，第 %7 张，共 %8 张；非文件内容预览")
            .arg(a3_ ? "A3" : "A4").arg(pages_).arg(copies_).arg(color_ ? QStringLiteral("彩色") : QStringLiteral("黑白"),
                duplex_ ? QStringLiteral("双面") : QStringLiteral("单面"), back_ ? QStringLiteral("背面") : QStringLiteral("正面"))
            .arg(sheet_ + 1).arg(sheetCount()) + (back_ && duplex_ && sheet_ * 2 + 2 > pages_ ? QStringLiteral("，背面留白") : QString()));
    }
    QVariantAnimation turning_, spreading_, sizing_;
    QPushButton* flip_ = nullptr;
    QPushButton* unfold_ = nullptr;
    QString title_; int pages_ = 8, copies_ = 1, sheet_ = 0;
    qreal turn_ = 0, spread_ = 0, size_ = 1; QPointF pointer_;
    bool a3_ = false, color_ = false, duplex_ = false, chosen_ = false, back_ = false, expanded_ = false, motion_ = true;
};
} // namespace cps::gui
