#pragma once

#include "print_controller.hpp"
#include "ui_theme.hpp"
#include "view_text.hpp"
#include <QPainterPath>
#include <QMouseEvent>
#include <QPushButton>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <functional>

namespace cps::gui {
namespace paper {
inline const QColor ink("#24273B"), muted("#62687D"), blue("#4548E8");
inline void text(QPainter& p, QRectF r, const QString& value, int size = 13,
                 QColor color = ink, bool bold = false, int align = Qt::AlignLeft | Qt::AlignVCenter) {
    QFont f = p.font(); f.setPixelSize(size); f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    p.setFont(f); p.setPen(color);
    p.drawText(r, align, p.fontMetrics().elidedText(value, Qt::ElideRight, static_cast<int>(r.width())));
}
inline void document(QPainter& p, QRectF rect, const QString& title, int printed, int sheets, bool small = false) {
    p.save(); p.translate(rect.topLeft()); p.scale(rect.width() / 230., rect.height() / 302.);
    p.setPen(Qt::NoPen);
    for (int i = 10; i > 0; --i) {
        p.setBrush(QColor(39, 55, 90, 3));
        p.drawRoundedRect(QRectF(-i * .5, i * 1.5, 230 + i, 302), 3, 3);
    }
    QPainterPath page;
    page.moveTo(0, 0); page.lineTo(230, 0); page.lineTo(230, 257);
    page.quadTo(225, 295, 183, 302); page.lineTo(0, 302); page.closeSubpath();
    p.setBrush(Qt::white); p.setPen(QPen(QColor("#E1E5EE"), .7)); p.drawPath(page);
    p.setPen(QPen(blue, 1.1)); p.drawLine(20, 29, 209, 29);
    text(p, {20, 9, 190, 15}, QStringLiteral("校园打印  /  任务单"), 7, muted);
    text(p, {20, 43, 190, 24}, title.isEmpty() ? QStringLiteral("准备好下一份灵感") : title, 13, ink, true);
    text(p, {20, 72, 190, 18}, QStringLiteral("文档信息"), 9, ink, true);
    for (int i = 0; i < 3; ++i) {
        p.setPen(QPen(QColor("#CBD2DE"), small ? 2.5 : 1.6));
        p.drawLine(QPointF(20, 100 + i * 9), QPointF(i == 2 ? 155 : 207, 100 + i * 9));
    }
    const std::array<QColor, 3> colors{blue, QColor("#DC8A74"), QColor("#61ACA2")};
    for (int i = 0; i < 3; ++i) {
        QColor fill = colors[i]; fill.setAlpha(30); p.setBrush(fill); p.setPen(QPen(colors[i], .7));
        p.drawRoundedRect(QRectF(20 + i * 64, 139, 55, 35), 3, 3);
        if (!small) text(p, {20. + i * 64, 139, 55, 35},
            QStringList{QStringLiteral("提交"), QStringLiteral("调度"), QStringLiteral("输出")}[i], 9, colors[i], true, Qt::AlignCenter);
    }
    text(p, {20, 189, 185, 18}, QStringLiteral("打印进度"), 9, ink, true);
    const double ratio = sheets > 0 ? std::clamp(double(printed) / sheets, 0., 1.) : 0.;
    for (int i = 0; i < 12; ++i) {
        p.setPen(Qt::NoPen); p.setBrush(i < ratio * 12 ? blue : QColor("#E4E9F3"));
        p.drawRoundedRect(QRectF(20 + i * 15.5, 239 - (i % 3) * 7, 10, 12 + (i % 3) * 7), 1.5, 1.5);
    }
    if (!small) text(p, {20, 260, 151, 15}, sheets > 0
        ? QStringLiteral("%1 / %2 页 · 任务示意").arg(printed).arg(sheets)
        : QStringLiteral("任务示意 · 非文件预览"), 7, muted);
    QPainterPath fold; fold.moveTo(183, 302); fold.quadTo(199, 283, 194, 261);
    fold.quadTo(215, 265, 230, 257); fold.quadTo(225, 295, 183, 302);
    QLinearGradient shade(192, 266, 222, 294);
    shade.setColorAt(0, QColor("#FFFFFF")); shade.setColorAt(.7, QColor("#F2F4F9")); shade.setColorAt(1, QColor("#DDE2EB"));
    p.setBrush(shade); p.setPen(QPen(QColor("#DFE4EC"), .6)); p.drawPath(fold); p.restore();
}
inline void printer(QPainter& p, QRectF r) {
    p.save(); p.translate(r.topLeft()); p.scale(r.width() / 68., r.height() / 53.);
    p.setPen(Qt::NoPen); p.setBrush(QColor(50, 60, 80, 15)); p.drawEllipse(QRectF(2, 43, 63, 9));
    QLinearGradient body(0, 10, 0, 48); body.setColorAt(0, Qt::white); body.setColorAt(1, QColor("#DDE3EB"));
    p.setPen(QPen(QColor("#CCD4DE"), .8)); p.setBrush(body); p.drawRoundedRect(QRectF(3, 12, 61, 32), 6, 6);
    p.setBrush(QColor("#EDF0F5")); p.drawRoundedRect(QRectF(10, 4, 48, 15), 3, 3);
    p.setBrush(QColor("#A2ACBB")); p.drawRoundedRect(QRectF(18, 7, 31, 4), 1, 1);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#37455B")); p.drawRoundedRect(QRectF(13, 33, 38, 7), 2, 2);
    p.setBrush(Qt::white); p.drawRect(QRectF(19, 36, 27, 10));
    p.setBrush(blue); p.drawEllipse(QRectF(53, 24, 3, 3)); p.restore();
}
} // namespace paper

// Only document artwork is painted; each selectable queue row also has a native,
// keyboard-focusable button. All values come from the controller snapshot.
class PaperWorkspace : public QWidget {
public:
    explicit PaperWorkspace(QWidget* parent = nullptr) : QWidget(parent), animation_(this) {
        setMinimumHeight(375); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setObjectName("paperWorkspace");
        setMouseTracking(true);
        setAccessibleName(QStringLiteral("文档预览与等待队列"));
        animation_.setDuration(430); animation_.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            phase_ = v.toReal(); positionButtons(); update();
        });
        for (int i = 0; i < 4; ++i) {
            rows_[i] = new QPushButton(this);
            rows_[i]->setObjectName(QStringLiteral("waitingJob%1").arg(i));
            rows_[i]->setProperty("role", "queueHit");
            rows_[i]->setCursor(Qt::PointingHandCursor);
            connect(rows_[i], &QPushButton::clicked, this, [this, i] {
                if (i >= waiting_.size()) return;
                select(waiting_[i].id); if (selectJob) selectJob(selected_);
            });
        }
    }
    void setMotion(bool enabled) {
        motion_ = enabled;
        if (!enabled) { animation_.stop(); phase_ = 1; tilt_ = 0; positionButtons(); update(); }
    }
    void select(const QString& id) {
        if (id == selected_) return;
        selected_ = id; animate(); update();
    }
    QString selectedId() const { return selected_; }
    QStringList waitingIds() const {
        QStringList ids; for (const auto& j : waiting_) ids << j.id; return ids;
    }
    void setView(const ViewState& view) {
        QStringList before = waitingIds();
        const auto previous = find(selected_);
        view_ = view;
        waiting_.clear();
        for (const auto& id : view.waitingOrder) {
            auto it = std::find_if(view.jobs.cbegin(), view.jobs.cend(), [&](const JobView& j) { return j.id == id; });
            if (it != view.jobs.cend()) waiting_.append(*it);
            if (waiting_.size() == 4) break;
        }
        const auto current = find(selected_);
        const bool completed = previous.state == JobState::Running && current.state == JobState::Completed && !selected_.isEmpty();
        if (completed) archived_ = current.title;
        if (selected_.isEmpty() || (completed && !view.printers[0].jobId.isEmpty())) {
            for (const auto& printer : view.printers) {
                if (!printer.jobId.isEmpty()) { selected_ = printer.jobId; break; }
            }
            if (selected_.isEmpty() && !waiting_.isEmpty()) selected_ = waiting_.front().id;
            if (selected_.isEmpty() && !view.jobs.isEmpty()) selected_ = view.jobs.back().id;
        }
        if (before != waitingIds() || completed) { previousOrder_ = before; animate(); }
        for (int i = 0; i < 4; ++i) {
            rows_[i]->setVisible(i < waiting_.size());
            if (i < waiting_.size()) {
                rows_[i]->setAccessibleName(QStringLiteral("预览 %1，%2 页，排队第 %3").arg(waiting_[i].title).arg(waiting_[i].sheets).arg(i + 1));
                rows_[i]->setToolTip(waiting_[i].title);
            }
        }
        positionButtons(); update();
    }
    std::function<void(const QString&)> selectJob;
protected:
    void mouseMoveEvent(QMouseEvent* event) override {
        if (motion_) { tilt_ = std::clamp((event->position().x() / width() - .35) * 6., -2., 2.); update(); }
    }
    void leaveEvent(QEvent*) override { tilt_ = 0; update(); }
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); positionButtons(); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const double split = width() * .66;
        p.fillRect(QRectF(0, 0, split, height()), QColor("#F0F1FB"));
        p.fillRect(QRectF(split, 0, width() - split, height()), QColor("#FFFFFF"));
        p.setPen(QColor("#E3E8F0")); p.drawLine(QPointF(split, 0), QPointF(split, height()));
        const JobView job = find(selected_);
        QString state = QStringLiteral("准备就绪");
        if (!job.id.isEmpty()) {
            switch (job.state) {
            case JobState::Running: state = QStringLiteral("正在打印"); break;
            case JobState::Queued: state = QStringLiteral("等待派发"); break;
            case JobState::Completed: state = QStringLiteral("已完成归档"); break;
            case JobState::Failed: state = QStringLiteral("打印失败"); break;
            case JobState::Cancelled: state = QStringLiteral("已取消"); break;
            case JobState::Cancelling: state = QStringLiteral("正在取消"); break;
            }
        }
        const QColor statusColor = job.state == JobState::Failed ? QColor("#C35B55") : paper::blue;
        const double copyWidth = split * .48 - 44;
        p.setPen(Qt::NoPen); p.setBrush(statusColor); p.drawEllipse(QRectF(30, 46, 7, 7));
        paper::text(p, {45, 34, copyWidth, 30}, state, 13, statusColor, true);
        paper::text(p, {30, 87, copyWidth, 46}, job.id.isEmpty() ? QStringLiteral("从这里，开始成稿。") : job.title,
                    width() < 1180 ? 22 : 27, paper::ink, true);
        QString device;
        const auto fleet = view_.shop.value("fleet").toArray();
        for (int i = 0; i < static_cast<int>(view_.printers.size()); ++i) if (!job.id.isEmpty() && view_.printers[i].jobId == job.id)
            device = QStringLiteral(" · ") + text::deviceName(fleet[i].toObject(), i);
        paper::text(p, {30, 139, copyWidth, 27}, job.id.isEmpty() ? QStringLiteral("柜台下单，或从学生端提交文档")
            : QStringLiteral("%1 页 · 优先级 %2").arg(job.sheets).arg(job.priority) + device +
              (job.estimatedFinishMs >= 0 ? QStringLiteral(" · ") + job.detail : QString()), 12, paper::muted);
        const int percent = job.sheets > 0 ? job.printed * 100 / job.sheets : 0;
        if (!job.id.isEmpty()) {
            paper::text(p, {30, 181, copyWidth, 22}, job.detail, 12, paper::muted);
            paper::text(p, {30, 207, copyWidth, 22}, QStringLiteral("当前选择 · ") + text::pickupCode(job.id), 11, paper::muted);
        }
        const double bottom = height() - 113;
        paper::text(p, {30, bottom, copyWidth, 45}, job.id.isEmpty() ? QStringLiteral("准备好了") : QString::number(percent) + "%",
                    job.id.isEmpty() ? 22 : 35, paper::ink, true);
        p.setPen(Qt::NoPen); p.setBrush(QColor("#CFDFD9"));
        p.drawRoundedRect(QRectF(30, bottom + 54, copyWidth - 10, 5), 2.5, 2.5);
        if (percent > 0) { p.setBrush(paper::blue); p.drawRoundedRect(QRectF(30, bottom + 54, (copyWidth - 10) * percent / 100., 5), 2.5, 2.5); }
        paper::text(p, {30, bottom + 66, copyWidth, 23}, job.id.isEmpty() ? QStringLiteral("支持批量导入与多设备调度")
            : QStringLiteral("已输出 %1 / %2 页").arg(job.printed).arg(job.sheets), 11, paper::muted);
        const double paperHeight = std::min(double(height() - 88), (split * .45 - 25) * 302 / 230.);
        QRectF sheet(split * .54, (height() - paperHeight) / 2., paperHeight * 230 / 302., paperHeight);
        p.save(); p.translate(sheet.center()); p.rotate(-6 + tilt_ + (1 - phase_) * 9); p.translate(-sheet.center());
        p.setPen(QColor("#E0E4EB")); p.setBrush(QColor("#F7F8FA"));
        p.drawRect(sheet.translated(17, 9)); p.setBrush(QColor("#FCFCFD")); p.drawRect(sheet.translated(8, 4));
        paper::document(p, sheet.translated(0, (1 - phase_) * 9), job.title, job.printed, job.sheets); p.restore();
        const double qx = split + 26, qw = width() - split - 52;
        paper::text(p, {qx, 22, qw, 37}, QStringLiteral("等待队列   %1").arg(view_.metrics.queued), 19, paper::ink, true);
        if (waiting_.isEmpty()) {
            paper::text(p, {qx, 125, qw, 35}, QStringLiteral("每一份，都安排妥当"), 15, paper::ink, true);
            paper::text(p, {qx, 161, qw, 26}, QStringLiteral("新任务将在这里有序等待"), 12, paper::muted);
        }
        for (int i = 0; i < waiting_.size(); ++i) {
            const auto& j = waiting_[i]; QRectF row = queueRect(i);
            if (j.id == selected_) p.fillRect(row.adjusted(-9, 0, 9, 0), QColor("#EEEEFC"));
            paper::document(p, {row.left() + 2, row.top() + 11, 36, 47}, {}, 0, j.sheets, true);
            paper::text(p, {row.left() + 51, row.top() + 10, row.width() - 82, 28}, j.title, 13, paper::ink, true);
            paper::text(p, {row.left() + 51, row.top() + 36, row.width() - 61, 22},
                        QStringLiteral("%1 页  ·  ").arg(j.sheets) + (j.estimatedFinishMs < 0 ? QStringLiteral("优先级 %1").arg(j.priority)
                            : view_.paused ? QStringLiteral("派发已暂停") : text::startsIn(j.estimatedStartMs - view_.nowMs)), 11, paper::muted);
            paper::text(p, {row.right() - 25, row.top() + 10, 25, 25}, QString("#%1").arg(i + 1), 11, paper::muted);
            p.setPen(QColor("#E7E9EE")); p.drawLine(row.bottomLeft(), row.bottomRight());
        }
        if (view_.metrics.queued > 4) paper::text(p, {qx, height() - 25., qw, 20},
            QStringLiteral("还有 %1 份文档 · 在记录中查看").arg(view_.metrics.queued - 4), 10, paper::muted);
        if (!archived_.isEmpty() && phase_ < 1) {
            p.save(); p.setOpacity(1 - phase_);
            paper::document(p, {split - 90 + phase_ * 42, height() - 100 + phase_ * 52, 40 * (1 - phase_ * .5), 53 * (1 - phase_ * .5)}, archived_, 1, 1, true);
            p.restore();
        }
    }
private:
    JobView find(const QString& id) const {
        for (const auto& j : view_.jobs) if (j.id == id) return j;
        return {};
    }
    QRectF queueRect(int i) const {
        const double split = width() * .66, h = std::min(78., (height() - 92.) / 4.);
        const int old = previousOrder_.indexOf(waiting_[i].id);
        const double from = old >= 0 ? old : i + .4;
        return {split + 26, 66 + (from + (i - from) * phase_) * h, width() - split - 52, h};
    }
    void positionButtons() {
        for (int i = 0; i < waiting_.size(); ++i) rows_[i]->setGeometry(queueRect(i).toRect());
    }
    void animate() {
        if (!motion_ || !isVisible()) { phase_ = 1; return; }
        animation_.stop(); animation_.setStartValue(0.); animation_.setEndValue(1.); animation_.start();
    }
    ViewState view_;
    QList<JobView> waiting_;
    QString selected_, archived_;
    QStringList previousOrder_;
    std::array<QPushButton*, 4> rows_{};
    QVariantAnimation animation_;
    qreal phase_ = 1;
    qreal tilt_ = 0;
    bool motion_ = true;
};

class PrinterTimeline : public QWidget {
public:
    explicit PrinterTimeline(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(188); setObjectName("printerTimeline");
        setAccessibleName(QStringLiteral("多台打印机的实时进度与待调度顺序"));
    }
    void setView(const ViewState& view) { view_ = view; setMinimumHeight(36 + int(view.printers.size()) * 76); update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), Qt::white);
        const double left = 207, right = width() - 26, activeWidth = (right - left) * .53;
        paper::text(p, {left, 0, activeWidth, 24}, QStringLiteral("当前执行 · 实时页数"), 11, paper::muted);
        paper::text(p, {left + activeWidth + 25, 0, right - left - activeWidth - 25, 24},
                    QStringLiteral("待派发顺序 · 完成后分配空闲设备"), 10, paper::muted);
        const auto fleet = view_.shop.value("fleet").toArray();
        for (int i = 0; i < static_cast<int>(view_.printers.size()); ++i) {
            const auto& device = view_.printers[i]; const double y = 34 + i * 76;
            const auto profile = fleet[i].toObject();
            const bool idle = device.jobId.isEmpty(), accepting = profile.value("online").toBool(true);
            p.setPen(QColor("#E8ECF3")); p.drawLine(QPointF(24, y), QPointF(right, y));
            paper::printer(p, {30, y + 15, 62, 47});
            paper::text(p, {107, y + 13, 99, 25}, text::deviceName(profile, i), 12, paper::ink, true);
            paper::text(p, {107, y + 38, 99, 22}, !accepting ? (idle ? QStringLiteral("暂停接单") : device.status + QStringLiteral(" · 暂停接单"))
                : idle ? QStringLiteral("空闲 · 等待任务") : device.status,
                        10, !accepting ? QColor("#A77D43") : idle ? paper::muted : paper::blue);
            paper::text(p, {left, y + 12, activeWidth - 20, 26}, idle
                ? (accepting ? QStringLiteral("准备接收下一份文档") : QStringLiteral("已暂停接单，不会分配新订单")) : device.title, 12, paper::ink, true);
            const double ratio = device.total > 0 ? std::clamp(double(device.printed) / device.total, 0., 1.) : 0;
            QRectF bar(left, y + 48, activeWidth - 74, 5);
            p.setPen(Qt::NoPen); p.setBrush(QColor("#E5E9F1")); p.drawRoundedRect(bar, 2.5, 2.5);
            if (ratio > 0) { p.setBrush(paper::blue); p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * ratio, 5), 2.5, 2.5); }
            paper::text(p, {bar.right() + 12, y + 37, 62, 24}, QString("%1%").arg(int(ratio * 100)), 11, paper::muted);
        }
        // The queue is shared, not a promise of future device assignments/timing.
        for (int i = 0; i < std::min(4, int(view_.waitingOrder.size())); ++i) {
            const auto id = view_.waitingOrder[i];
            auto job = std::find_if(view_.jobs.cbegin(), view_.jobs.cend(), [&](const JobView& j) { return j.id == id; });
            if (job == view_.jobs.cend()) continue;
            const double w = (right - left - activeWidth - 33) / 2;
            QRectF r(left + activeWidth + 25 + (i % 2) * (w + 8), 48 + (i / 2) * 76, w, 43);
            p.setPen(Qt::NoPen); p.setBrush(QColor("#F0F3F8")); p.drawRoundedRect(r, 5, 5);
            paper::text(p, r.adjusted(11, 0, -8, 0), QString("%1  ").arg(i + 1) + job->title, 11, paper::muted);
        }
    }
private:
    ViewState view_;
};
} // namespace cps::gui
