#pragma once
#include "ui_theme.hpp"
#include "document_preview.hpp"
#include "editorial_chrome.hpp"
#include "paper_transition.hpp"
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QScrollArea>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <cmath>
#include <array>

namespace cps::gui {
class EditorialPreviewArea : public QScrollArea {
  public:
    std::function<void()> viewportChanged;
  protected:
    void resizeEvent(QResizeEvent *event) override {
        QScrollArea::resizeEvent(event);
        QTimer::singleShot(0, this, [this] { if (viewportChanged) viewportChanged(); });
    }
};
// Every paper is independently drawn and contains an actual document page.
class PrintProof : public QWidget {
  public:
    explicit PrintProof(QWidget *parent = nullptr)
        : QWidget(parent), spreading_(this), aligning_(this), turning_(this) {
        setObjectName("printProof");
        setMinimumSize(0, 0);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        setAttribute(Qt::WA_OpaquePaintEvent);
        setMouseTracking(true);
        desk_.load(":/editorial/desk.png");
        campus_.load(":/editorial/campus.png");
        bind(spreading_, spread_);
        spreading_.setEasingCurve(QEasingCurve::Linear);
        spreading_.setDuration(240);
        bind(aligning_, alignment_);
        bind(turning_, turn_);
        describe();
        setToolTip(QStringLiteral("悬停纸张展开，移开收起；双击放大；方向键翻页，空格翻面"));
    }
    bool loadDocument(const QString &path) {
        closeReaders();
        turning_.stop();
        bool ok = document_.load(path);
        paperCache_.clear();
        sheet_ = 0;
        back_ = false;
        turn_ = 0;
        update();
        return ok;
    }
    void clearDocument() {
        closeReaders();
        turning_.stop();
        document_.clear();
        paperCache_.clear();
        sheet_ = 0;
        back_ = false;
        turn_ = 0;
        update();
    }
    int documentPages() const { return document_.pageCount(); }
    QString previewError() const { return document_.error(); }
    QImage renderedPage(int index) { return document_.page(index); }
    void setSpecification(const QString &title, int pages, int copies, bool a3, bool color, bool duplex,
                          bool chosen) {
        if (pages_ != pages || color_ != color || duplex_ != duplex || chosen_ != chosen)
            closeReaders();
        if (pages_ != pages || color_ != color || chosen_ != chosen || a3_ != a3)
            paperCache_.clear();
        title_ = title;
        pages_ = std::max(1, pages);
        copies_ = copies;
        a3_ = a3;
        color_ = color;
        duplex_ = duplex;
        chosen_ = chosen;
        sheet_ = std::min(sheet_, sheetCount() - 1);
        if (!duplex_)
            setBack(false);
        describe();
        update();
        if (changed)
            changed();
    }
    void setBack(bool back) {
        back_ = back && duplex_;
        animate(turning_, turn_, back_ ? 1. : 0.);
        describe();
        if (changed)
            changed();
    }
    void setExpanded(bool open) {
        expanded_ = open;
        if (!open) {
            hoverSuppressed_ = onPaper(hoverPosition_, 0.) || onPaper(hoverPosition_, 1.);
            hovered_ = false;
        }
        animate(spreading_, spread_, expanded_ || hovered_ ? 1. : 0.);
        describe();
        if (changed)
            changed();
    }
    void setAligned(bool aligned) {
        aligned_ = aligned;
        animate(aligning_, alignment_, aligned ? 1. : 0.);
        describe();
        if (changed)
            changed();
    }
    void stepPage(int dir) {
        sheet_ = std::clamp(sheet_ + dir, 0, sheetCount() - 1);
        describe();
        update();
        if (changed)
            changed();
    }
    int currentSheet() const { return sheet_ + 1; }
    int currentPage() const { return duplex_ ? sheet_ * 2 + (back_ ? 1 : 0) : sheet_; }
    int sheetCount() const { return duplex_ ? (pages_ + 1) / 2 : pages_; }
    bool showingBack() const { return back_; }
    bool expanded() const { return expanded_; }
    bool hoverExpanded() const { return hovered_; }
    qreal expansionProgress() const { return spread_; }
    bool aligned() const { return aligned_; }
    bool motionEnabled() const { return motion_; }
    void setMotion(bool enabled) {
        motion_ = enabled;
        if (!enabled) {
            hovered_ = false;
            hoverSuppressed_ = false;
            spreading_.stop();
            aligning_.stop();
            turning_.stop();
            spread_ = expanded_ ? 1. : 0.;
            alignment_ = aligned_ ? 1. : 0.;
            turn_ = back_ ? 1. : 0.;
            update();
        }
    }
    QString specification() const { return accessibleDescription(); }
    void attachActions(QPushButton *flip, QPushButton *unfold) {
        flip_ = flip;
        unfold_ = unfold;
        for (auto *b : {flip_, unfold_}) {
            b->setParent(this);
            b->show();
        }
        positionActions();
    }
    void attachAlignment(QPushButton *align) {
        align_ = align;
        align_->setParent(this);
        align_->show();
        positionActions();
    }
    std::function<void()> changed;
    void openReader() {
        closeReaders();
        auto *dialog = new EditorialDialog(this);
        // QDialog::open() otherwise replaces macOS flags with Qt::Sheet.
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setObjectName("documentReader");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(title_.isEmpty() ? QStringLiteral("示例文档 · 放大预览")
                                                : title_ + QStringLiteral(" · 放大预览"));
        dialog->setMinimumSize(720, 560);
        const auto available = dialog->screen()->availableGeometry().size();
        dialog->resize(std::min(1000, available.width() - 60), std::min(820, available.height() - 60));
        auto *body = new QVBoxLayout(dialog);
        body->setContentsMargins(1, 1, 1, 1);
        body->setSpacing(0);
        body->addWidget(editorialDialogHeader(dialog, QStringLiteral("纸间 · 印前预览"),
                                             title_.isEmpty() ? QStringLiteral("示例文档") : title_));
        auto *scroll = new EditorialPreviewArea;
        scroll->setObjectName("readerScroll");
        scroll->setAlignment(Qt::AlignCenter);
        scroll->viewport()->setStyleSheet("background:#EAE7E1;");
        auto *image = new QLabel;
        image->setObjectName("readerImage");
        image->setStyleSheet("background:white;color:#69748A;font-size:18px;");
        auto *shadow = new QGraphicsDropShadowEffect(image);
        shadow->setBlurRadius(24);
        shadow->setOffset(0, 5);
        shadow->setColor(QColor(23, 30, 41, 42));
        image->setGraphicsEffect(shadow);
        scroll->setWidget(image);
        body->addWidget(scroll, 1);
        auto *footer = new QFrame;
        footer->setObjectName("readerFooter");
        footer->setStyleSheet("QFrame#readerFooter{background:#FAFAF9;border-top:1px solid #DADDE2;}");
        footer->setFixedHeight(76);
        auto *bar = new QHBoxLayout(footer);
        bar->setContentsMargins(24, 12, 24, 12);
        bar->setSpacing(10);
        auto *prev = new QPushButton(QStringLiteral("‹  上一页"));
        prev->setObjectName("readerPrevious");
        auto *next = new QPushButton(QStringLiteral("下一页  ›"));
        next->setObjectName("readerNext");
        auto *count = new QLabel;
        count->setObjectName("readerPageCount");
        count->setAlignment(Qt::AlignCenter);
        count->setMinimumWidth(110);
        auto *fit = new QPushButton(QStringLiteral("适合窗口"));
        fit->setObjectName("readerFit");
        fit->setCheckable(true);
        fit->setChecked(true);
        auto *zoom = new QPushButton(QStringLiteral("125%"));
        zoom->setCheckable(true);
        zoom->setObjectName("readerZoom");
        bar->addWidget(prev);
        bar->addWidget(count);
        bar->addWidget(next);
        bar->addStretch();
        bar->addWidget(fit);
        bar->addWidget(zoom);
        for (auto *button : {prev, next, fit, zoom}) {
            button->setAutoDefault(false);
            button->setMinimumHeight(42);
        }
        body->addWidget(footer);
        auto index = std::make_shared<int>(currentPage());
        const auto refresh = [this, index, image, count, prev, next, zoom, scroll] {
            const bool blank = *index >= pages_;
            QImage rendered = blank ? QImage() : chosen_ ? document_.page(*index) : sample(*index);
            if (rendered.isNull()) {
                image->clear();
                image->setText(blank ? QStringLiteral("背面留白") : document_.error().isEmpty() ? QStringLiteral("这一页不在原文件中")
                                                           : document_.error());
                image->setAlignment(Qt::AlignCenter);
                image->resize(520, 600);
            } else {
                if (!color_)
                    rendered = rendered.convertToFormat(QImage::Format_Grayscale8);
                const QSize fitted = rendered.size().scaled(
                    std::max(240, scroll->viewport()->width() - 72),
                    std::max(240, scroll->viewport()->height() - 48), Qt::KeepAspectRatio);
                const int w = zoom->isChecked() ? 1000 : std::min(700, fitted.width());
                image->setPixmap(QPixmap::fromImage(rendered).scaledToWidth(w, Qt::SmoothTransformation));
                image->resize(image->pixmap().size());
            }
            const int n = chosen_ ? document_.pageCount() : pages_;
            count->setText(n == 0 ? QStringLiteral("无法预览") : blank ? QStringLiteral("末张背面 · 留白")
                                 : QStringLiteral("第 %1 / %2 页").arg(*index + 1).arg(n));
            prev->setEnabled(n > 0 && *index > 0);
            next->setEnabled(*index + 1 < n);
        };
        connect(prev, &QPushButton::clicked, dialog, [index, refresh, scroll] {
            --*index;
            refresh();
            scroll->verticalScrollBar()->setValue(0);
        });
        connect(next, &QPushButton::clicked, dialog, [index, refresh, scroll] {
            ++*index;
            refresh();
            scroll->verticalScrollBar()->setValue(0);
        });
        connect(zoom, &QPushButton::toggled, dialog, [refresh, fit](bool checked) {
            fit->setChecked(!checked);
            refresh();
        });
        connect(fit, &QPushButton::clicked, dialog, [zoom, fit, refresh] {
            zoom->setChecked(false);
            fit->setChecked(true);
            refresh();
        });
        scroll->viewportChanged = [zoom, refresh] { if (!zoom->isChecked()) refresh(); };
        for (const auto key : {Qt::Key_Left, Qt::Key_PageUp})
            connect(new QShortcut(QKeySequence(key), dialog), &QShortcut::activated, prev, &QPushButton::click);
        for (const auto key : {Qt::Key_Right, Qt::Key_PageDown})
            connect(new QShortcut(QKeySequence(key), dialog), &QShortcut::activated, next, &QPushButton::click);
        connect(new QShortcut(QKeySequence("Ctrl+0"), dialog), &QShortcut::activated, fit, &QPushButton::click);
        refresh();
        dialog->open();
    }

  protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverMove)
            hoverAt(mapFromGlobal(static_cast<QHoverEvent *>(event)->globalPosition().toPoint()));
        else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Hide) {
            hoverPosition_ = {-1000, -1000};
            hoverSuppressed_ = false;
            setHovered(false);
        }
        return QWidget::event(event);
    }
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        positionActions();
    }
    void mouseDoubleClickEvent(QMouseEvent *) override { openReader(); }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Right)
            stepPage(1);
        else if (e->key() == Qt::Key_Left)
            stepPage(-1);
        else if (e->key() == Qt::Key_Space)
            setBack(!back_);
        else if (e->key() == Qt::Key_Return)
            openReader();
        else
            QWidget::keyPressEvent(e);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor("#F4F3EF"));
        if (!desk_.isNull()) {
            const qreal dpr = devicePixelRatioF();
            const QSize pixels = (QSizeF(size()) * dpr).toSize();
            if (deskLayer_.size() != pixels || deskLayer_.devicePixelRatio() != dpr) {
                deskLayer_ = desk_.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                deskLayer_.setDevicePixelRatio(dpr);
            }
            p.drawPixmap(QPoint(0, 0), deskLayer_);
        }
        p.scale(width() / 1218., height() / 788.);
        text(p, {42, 23, 700, 78}, QStringLiteral("好作品，值得"), 60, QColor("#101F3C"), true, true);
        const qreal accentX=42+p.fontMetrics().horizontalAdvance(QStringLiteral("好作品，值得"));
        text(p, {accentX, 23, 370, 78}, QStringLiteral("印出来。"), 60, QColor("#D63D29"), true, true);
        text(p, {44, 109, 750, 42}, QStringLiteral("拖入文件，在纸上调整你的打印方式。"), 24,
             QColor("#667080"));
        const auto papers = paperPlacements(spread_);
        drawPaper(p, papers[0].center, papers[0].size, papers[0].angle, std::max(0, currentPage() - 1), 0);
        drawPaper(p, papers[1].center, papers[1].size, papers[1].angle,
                  std::min(pages_ - 1, currentPage() + (chosen_ ? 1 : 2)), 1);
        const qreal squash = std::max(.06, std::abs(std::cos(turn_ * 3.141592653589793)));
        drawPaper(p, papers[2].center, {papers[2].size.width() * squash, papers[2].size.height()},
                  papers[2].angle, currentPage() + (chosen_ ? 0 : 1), 2);
        text(p, {470, 752, 590, 27},
             chosen_ && currentPage() >= pages_ ? QStringLiteral("末张背面留白 · 双击放大")
             : chosen_ ? QStringLiteral("第 %1 / %2 页 · %3 · 双击放大")
                           .arg(currentPage() + 1)
                           .arg(pages_)
                           .arg(back_ ? QStringLiteral("背面") : QStringLiteral("正面"))
                     : QStringLiteral("示例文档 · 拖入你的文件开始预览"),
             14, QColor("#596477"));
    }

  private:
    struct PaperPlacement { QPointF center; QSizeF size; qreal angle; };
    std::array<PaperPlacement, 3> paperPlacements(qreal spread) const {
        const qreal scale = a3_ ? 1.06 : 1.;
        return {{{{282 - spread * 60 - alignment_ * 30, 476 - alignment_ * 12 - spread * 40 * (1. - alignment_)},
                  {340 - spread * 30, 555 - alignment_ * 15 - spread * 30}, (1. - alignment_) * 11.},
                 {{1029 + spread * 20 + alignment_ * 25, 462 + alignment_ * 2 - spread * 40 * (1. - alignment_)},
                  {342 - spread * 30, 500 + alignment_ * 40 - spread * 30}, (1. - alignment_) * 9.},
                 {{654, 432 + alignment_ * 32},
                  {(438 - spread * 60) * scale, (610 - alignment_ * 70 - spread * 30) * scale}, (1. - alignment_) * -8.}}};
    }
    bool onPaper(QPointF point, qreal spread) const {
        for (const auto &paper : paperPlacements(spread)) {
            QTransform transform;
            transform.translate(paper.center.x(), paper.center.y());
            transform.rotate(paper.angle);
            const auto local = transform.inverted().map(point);
            if (QRectF(-paper.size.width() / 2., -paper.size.height() / 2.,
                       paper.size.width(), paper.size.height()).contains(local)) return true;
        }
        return false;
    }
    void hoverAt(QPointF local) {
        if (width() <= 0 || height() <= 0) return;
        const QPointF point(local.x() * 1218. / width(), local.y() * 788. / height());
        hoverPosition_ = point;
        if (!motion_ || expanded_) return;
        // Keep the complete motion envelope active after entry, so moving paper
        // edges and the overlaid tools cannot repeatedly trigger open/close.
        const bool inside = onPaper(point, spread_) || ((hovered_ || hoverSuppressed_)
            && (onPaper(point, 0.) || onPaper(point, 1.)));
        if (hoverSuppressed_) {
            if (!inside) hoverSuppressed_ = false;
            return;
        }
        setHovered(inside);
    }
    void setHovered(bool hover) {
        if (hovered_ == hover) return;
        hovered_ = hover;
        animate(spreading_, spread_, expanded_ || hovered_ ? 1. : 0.);
    }
    void closeReaders() {
        for (auto *reader : findChildren<QDialog *>("documentReader"))
            reader->close();
    }
    static void text(QPainter &p, QRectF rect, const QString &value, int size, QColor ink, bool bold = false,
                     bool serif = false) {
        QFont f(serif ? QStringLiteral("SimSun") : QStringLiteral("Microsoft YaHei"));
        f.setPixelSize(size);
        f.setBold(bold);
        p.setFont(f);
        p.setPen(ink);
        p.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, value);
    }
    void drawPaper(QPainter &p, QPointF center, QSizeF size, qreal angle, int page, int slot) {
        p.save();
        p.translate(center);
        p.rotate(angle);
        const QRectF box(-size.width() / 2, -size.height() / 2, size.width(), size.height());
        const qreal scale = a3_ ? 1.06 : 1.;
        const QSizeF base = slot == 0 ? QSizeF(340, 555) : slot == 1 ? QSizeF(342, 540)
                                        : QSizeF(438 * scale, 610 * scale);
        // Resolve content and texture once, outside the per-frame transformation.
        // Quantize the reference density, not animated dimensions, to keep cache hits stable.
        const qreal density = qBound(.5, std::ceil(devicePixelRatioF() *
                                     std::max(width() / 1218., height() / 788.) * 4) / 4, 3.);
        const QString key = QStringLiteral("%1:%2:%3").arg(slot).arg(page).arg(density);
        auto *sprite = paperCache_.object(key);
        if (!sprite) {
            const QRectF surface(0, 0, base.width() + 32, base.height() + 42);
            QPixmap raster((surface.size() * density).toSize());
            raster.setDevicePixelRatio(density);
            raster.fill(Qt::transparent);
            QPainter rasterPainter(&raster);
            rasterPainter.setRenderHint(QPainter::Antialiasing);
            rasterPainter.setRenderHint(QPainter::SmoothPixmapTransform);
            const QRectF paper(16, 10, base.width(), base.height());
            paintPaperSurface(rasterPainter, paper, page, slot == 2);
            rasterPainter.end();
            sprite = new QPixmap(raster);
            const int cost = std::max(1, raster.width() * raster.height() * 4 / 1024);
            paperCache_.insert(key, sprite, cost);
        }
        p.drawPixmap(box.adjusted(-16, -10, 16, 32), *sprite, QRectF(QPointF(), sprite->size()));
        p.restore();
    }
    void paintPaperSurface(QPainter &p, const QRectF &box, int page, bool selected) {
        p.drawPixmap(box.adjusted(-16, -10, 16, 32), paperShadow(selected), QRectF(0, 0, 432, 642));
        QLinearGradient paper(box.topLeft(), box.bottomRight());
        paper.setColorAt(0, QColor("#FFFFFF"));
        paper.setColorAt(1, QColor("#F3F3F4"));
        p.setBrush(paper);
        p.setPen(QPen(QColor("#E5E4E2"), 1));
        p.drawRect(box);
        QImage content;
        if (page >= pages_)
            text(p, box.adjusted(30, 0, -30, 0), QStringLiteral("背面留白"), 22, QColor("#8A8F98"));
        else
            content = chosen_ ? document_.page(page) : sample(page);
        if (!content.isNull()) {
            if (!color_)
                content = content.convertToFormat(QImage::Format_Grayscale8);
            const QSizeF fitted = content.size().scaled(box.size().toSize(), Qt::KeepAspectRatio);
            const QRectF target(box.center().x() - fitted.width() / 2, box.center().y() - fitted.height() / 2,
                                fitted.width(), fitted.height());
            p.drawImage(target, content);
        } else if (chosen_ && page < pages_)
            text(p, box.adjusted(24, 0, -24, 0),
                 document_.error().isEmpty() ? QStringLiteral("原文件没有这一页") : document_.error(), 15,
                 QColor("#7C8798"));
        if (!chosen_) {
            p.setOpacity(.09);
            p.drawPixmap(box, desk_, QRectF(430, 160, 570, 790));
            p.setOpacity(1.);
        }
    }
    const QPixmap &paperShadow(bool selected) {
        auto &shadow = selected ? frontShadow_ : rearShadow_;
        if (shadow.isNull()) {
            shadow = QPixmap(432, 642);
            shadow.fill(Qt::transparent);
            QPainter p(&shadow);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(216, 310);
            p.setPen(Qt::NoPen);
            const QRectF box(-200, -300, 400, 600);
            for (int i = 20; i > 0; --i) {
                p.setBrush(QColor(22, 26, 30, selected ? 4 : 2));
                p.drawRoundedRect(box.adjusted(-i * .55, -i * .3, i * .55, i * .85).translated(2, 6), 1, 1);
            }
        }
        return shadow;
    }
    QImage sample(int index) {
        if (auto *cached = sampleCache_.object(index))
            return *cached;
        QImage page(600, 850, QImage::Format_ARGB32);
        page.fill(QColor("#FCFCFC"));
        QPainter p(&page);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QColor navy("#112340"), red("#D6402C");
        if (index % 3 == 0) {
            text(p, {60, 70, 470, 35}, QStringLiteral("2024 年秋季学期"), 17, navy, false, true);
            text(p, {60, 152, 490, 135}, QStringLiteral("操作系统\n实验报告"), 56, navy, true, true);
            text(p, {60, 296, 490, 65}, "OPERATING SYSTEMS\nLABORATORY REPORT", 17, navy, false, true);
            if (!campus_.isNull()) {
                p.drawPixmap(QRect(57, 405, 488, 290), campus_);
            }
            text(p, {60, 730, 480, 60}, QStringLiteral("计算机科学与技术学院\n2024 年 12 月"), 15, navy);
        } else if (index % 3 == 1) {
            text(p, {55, 65, 370, 78}, "OPERATING\nSYSTEMS", 28, navy, true, true);
            text(p, {55, 156, 300, 65}, "LABORATORY\nREPORT", 15, navy, false, true);
            QPainterPath artwork;
            artwork.setFillRule(Qt::WindingFill);
            artwork.addRect(QRectF(288, 74, 156, 628));
            artwork.addEllipse(QRectF(55, 241, 320, 320));
            artwork.addEllipse(QRectF(55, 385, 500, 355));
            p.save();
            p.setClipPath(artwork);
            p.fillRect(QRect(288, 74, 156, 628), navy);
            p.setPen(Qt::NoPen);
            p.setBrush(red);
            p.drawEllipse(QRectF(55, 241, 320, 320));
            p.setBrush(navy);
            p.drawEllipse(QRectF(55, 385, 500, 355));
            p.setBrush(red);
            p.drawEllipse(QRectF(55, 241, 320, 320));
            if (!campus_.isNull()) {
                const QRectF source(0, campus_.height() * .27, campus_.width(), campus_.height() * .64);
                p.drawPixmap(QRectF(55, 526, 500, 210), campus_, source);
            }
            p.setBrush(navy);
            p.drawPolygon(QPolygonF{{55, 602}, {270, 738}, {55, 738}});
            p.drawPolygon(QPolygonF{{555, 612}, {341, 738}, {555, 738}});
            p.setPen(QColor(255, 255, 255, 46));
            for (int y = 72; y < 741; y += 3)
                for (int x = 53; x < 558; x += 3) {
                    const unsigned seed = unsigned(x * 73856093u) ^ unsigned(y * 19349663u);
                    if (seed % 5 == 0)
                        p.drawPoint(QPointF(x + (seed % 7) * .31, y + (seed % 11) * .17));
                }
            p.restore();
            p.setPen(QPen(QColor("#FFFFFF"), 1));
            p.drawLine(60, 520, 536, 578);
            p.drawLine(60, 433, 542, 464);
            text(p, {468, 72, 110, 45}, "2024", 15, red, true, true);
            text(p, {476, 192, 110, 65}, "IDEAS\nCODE\nEXPERIMENT", 10, navy);
            text(p, {468, 301, 125, 60}, QStringLiteral("操作系统\n实验报告"), 16, navy, true, true);
            p.setPen(QPen(navy, .8));
            p.drawLine(40, 384, 553, 520);
            p.drawLine(40, 477, 520, 452);
            p.drawLine(61, 303, 61, 764);
            p.drawLine(376, 525, 376, 798);
            p.drawEllipse(QRectF(61, 452, 485, 331));
            for (const QPointF mark : {QPointF(42, 28), QPointF(550, 29), QPointF(376, 798)}) {
                p.drawLine(mark - QPointF(12, 0), mark + QPointF(12, 0));
                p.drawLine(mark - QPointF(0, 12), mark + QPointF(0, 12));
            }
            text(p, {58, 779, 330, 28}, "2024.12     /     OS REPORT", 12, navy);
        } else {
            text(p, {55, 28, 480, 42}, QStringLiteral("实验内容   /   %1").arg(index + 1), 14, navy);
            p.setPen(navy);
            p.drawLine(55, 75, 545, 75);
            text(p, {55, 105, 490, 55}, QStringLiteral("2.1  进程调度的基本概念"), 23, navy, true, true);
            text(p, {55, 173, 500, 160},
                 QStringLiteral(
                     "进程是程序的一次执行过程。调度器根据队列\n和设备状态选择下一个任务，保障任务有序运行。"
                     "\n\n本实验通过观察就绪、运行、阻塞之间的转换，\n理解操作系统对共享资源的协调与管理。"),
                 17, QColor("#5D6370"));
            const QRectF ready(67, 400, 130, 60), run(382, 400, 130, 60), wait(229, 554, 130, 60);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(navy, 1.6));
            for (const auto &r : {ready, run, wait})
                p.drawRoundedRect(r, 6, 6);
            text(p, ready.adjusted(29, 0, 0, 0), QStringLiteral("就绪态"), 19, navy);
            text(p, run.adjusted(29, 0, 0, 0), QStringLiteral("运行态"), 19, navy);
            text(p, wait.adjusted(29, 0, 0, 0), QStringLiteral("阻塞态"), 19, navy);
            p.drawLine(ready.topRight() + QPointF(0, 30), run.topLeft() + QPointF(0, 30));
            p.drawLine(run.bottomLeft() + QPointF(40, 0), wait.topRight() + QPointF(-30, 0));
            p.drawLine(wait.topLeft() + QPointF(30, 0), ready.bottomRight() + QPointF(-40, 0));
            text(p, {223, 365, 210, 32}, QStringLiteral("时间片用尽"), 14, navy);
            text(p, {73, 495, 140, 32}, QStringLiteral("I/O 完成"), 14, navy);
            p.fillRect(QRect(55, 658, 490, 130), QColor("#EEEEF0"));
            text(p, {78, 673, 450, 96},
                 "struct Process {\n    int pid;\n    int state;\n    int priority;\n};", 15, navy);
        }
        p.end();
        sampleCache_.insert(index, new QImage(page));
        return page;
    }
    void positionActions() {
        const qreal sx = width() / 1218., sy = height() / 788.;
        int row = 0;
        for (auto *b : {unfold_, flip_, align_})
            if (b)
                b->setGeometry(qRound(873 * sx), qRound((289 + row++ * 66) * sy), qRound(61 * sx),
                               qRound(64 * sy));
    }
    void bind(QVariantAnimation &a, qreal &value) {
        a.setDuration(420);
        a.setEasingCurve(QEasingCurve::OutCubic);
        connect(&a, &QVariantAnimation::valueChanged, this, [this, &value](const QVariant &v) {
            value = v.toReal();
            update();
        });
    }
    void animate(QVariantAnimation &a, qreal &value, qreal target) {
        if (&a == &spreading_ && motion_) {
            spreading_.retarget(value, target);
            return;
        }
        a.stop();
        if (motion_) {
            a.setStartValue(value);
            a.setEndValue(target);
            a.start();
        } else {
            value = target;
            update();
        }
    }
    void describe() {
        setAccessibleName(QStringLiteral("打印文件预览"));
        setAccessibleDescription(
            QStringLiteral("%1，%2 页，%3 份，%4，%5，第 %6 张，共 %7 张%8")
                .arg(a3_ ? "A3" : "A4")
                .arg(pages_)
                .arg(copies_)
                .arg(color_ ? QStringLiteral("彩色") : QStringLiteral("黑白"),
                     duplex_ ? QStringLiteral("双面") : QStringLiteral("单面"))
                .arg(sheet_ + 1)
                .arg(sheetCount())
                .arg(back_ && currentPage() >= pages_ ? QStringLiteral("，背面留白") : QString()));
    }
    DocumentPreview document_;
    QCache<int, QImage> sampleCache_{8};
    QPixmap desk_, campus_, deskLayer_;
    QCache<QString, QPixmap> paperCache_{32768}; // 32 MiB, bounded independently of document length
    QPixmap frontShadow_, rearShadow_;
    PaperTransition spreading_;
    QVariantAnimation aligning_, turning_;
    QPushButton *flip_ = nullptr;
    QPushButton *unfold_ = nullptr;
    QPushButton *align_ = nullptr;
    QString title_;
    int pages_ = 8, copies_ = 1, sheet_ = 0;
    qreal spread_ = 0., alignment_ = 0., turn_ = 0.;
    QPointF hoverPosition_{-1000, -1000};
    bool a3_ = false, color_ = true, duplex_ = true, chosen_ = false, back_ = false, expanded_ = false,
         aligned_ = false, motion_ = true, hovered_ = false, hoverSuppressed_ = false;
};
} // namespace cps::gui
