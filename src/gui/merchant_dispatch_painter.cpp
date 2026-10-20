#include "merchant_dispatch_painter.hpp"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QStringList>
#include <algorithm>
#include <array>
#include <vector>

namespace cps::gui::merchant {
namespace {
constexpr qreal timelineLeft = 509;
constexpr qreal timelineWidth = 768;
constexpr qreal timelineBottom = 756;
constexpr std::array<qreal, 3> rowY{{407, 540, 673}};

QString minutes(cps::TimeMs value, int decimals = 1) {
    return QString::number(static_cast<double>(value) / 60000., 'f', decimals);
}

const DispatchDemoOrder* orderFor(const DispatchDemo& demo, const std::string& id) {
    const QString wanted = QString::fromStdString(id);
    const auto found = std::find_if(demo.orders.cbegin(), demo.orders.cend(),
        [&wanted](const DispatchDemoOrder& item) { return item.id == wanted; });
    return found == demo.orders.cend() ? nullptr : &*found;
}

void control(Painter& d, QRectF rect, const QString& label,
             const QString& action, const QString& icon, bool primary = false) {
    d.panel(rect, primary ? red : QColor(Qt::white), primary ? red : ruleColor, 6);
    const QColor ink = primary ? QColor(Qt::white) : navy;
    const qreal iconX = rect.x() + 27;
    if (icon == QStringLiteral("play")) {
        d.p.save();
        d.p.setPen(Qt::NoPen); d.p.setBrush(ink);
        QPainterPath triangle;
        triangle.moveTo(iconX + 3, rect.y() + 13);
        triangle.lineTo(iconX + 3, rect.y() + 34);
        triangle.lineTo(iconX + 20, rect.y() + 23.5);
        triangle.closeSubpath(); d.p.drawPath(triangle); d.p.restore();
    } else if (icon == QStringLiteral("pause")) {
        d.p.fillRect(QRectF(iconX + 4, rect.y() + 14, 5, 19), ink);
        d.p.fillRect(QRectF(iconX + 14, rect.y() + 14, 5, 19), ink);
    } else {
        d.icon(icon, iconX, rect.y() + 10, 27, ink);
    }
    d.text(rect.x() + 63, rect.y(), rect.width() - 73, rect.height(),
           label, 16, ink, primary, false, Qt::AlignCenter);
    d.hits.push_back({rect, action, {}, label, true, primary, true});
}

void toolbar(Painter& d, const DispatchDemo& demo, bool playing) {
    d.text(35, 232, 54, 45, QStringLiteral("场景："), 16);
    // The two editable choices are actual QComboBox widgets owned by the window.
    d.panel(QRectF(92, 231, 245, 46), Qt::white, ruleColor, 6);
    d.panel(QRectF(763, 231, 127, 46), Qt::white, ruleColor, 6);
    d.p.setPen(QPen(ruleColor, 1));
    d.p.drawLine(QPointF(366, 241), QPointF(366, 269));
    control(d, QRectF(395, 231, 162, 47), QStringLiteral("单步执行"),
            QStringLiteral("dispatch-step"), QStringLiteral("play"));
    control(d, QRectF(575, 231, 172, 47), QStringLiteral("重新开始"),
            QStringLiteral("dispatch-reset"), QStringLiteral("refresh"));
    control(d, QRectF(908, 231, 162, 47), playing ? QStringLiteral("暂停演示") : QStringLiteral("播放演示"),
            QStringLiteral("dispatch-play"), playing ? QStringLiteral("pause") : QStringLiteral("play"), true);
    d.text(1094, 230, 454, 24, demo.stress ? QStringLiteral("100 单 · %1 台异构设备 · 非抢占").arg(demo.printers.size()) :
        QStringLiteral("单台教学打印机 · 3 页/分钟 · 非抢占"), 13, slate);
    d.text(1094, 254, 454, 24, QStringLiteral("同一输入 · 等待 / 快速准时 / 贡献等权"), 13, slate);
}

void orders(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel(QRectF(18, 293, 286, 480), Qt::white, ruleColor, 4);
    d.text(35, 310, 148, 35, QStringLiteral("这一批订单"), 22, navy, true);
    int pages = 0;
    for (const auto& item : demo.orders) pages += item.job.pages * item.job.copies;
    d.text(35, 343, 250, 24, QStringLiteral("共 %1 单 · %2 面").arg(demo.orders.size()).arg(pages), 13, slate);
    const int offset = std::clamp(selection.orderOffset, 0, std::max(0, int(demo.orders.size()) - 1));
    for (int visible = 0; visible < 6 && offset + visible < demo.orders.size(); ++visible) {
        const auto& item = demo.orders[offset + visible];
        const QRectF card(34, 375 + visible * 54, 255, 48);
        const bool focus = item.id == selection.order;
        d.panel(card, focus ? QColor("#FFF8F4") : QColor(Qt::white), focus ? red : ruleColor, 4);
        d.panel(QRectF(45, card.y() + 9, 22, 30), item.color, item.color, 4);
        d.text(78, card.y()+1, 147, 25, item.id + QStringLiteral("  ") + item.title, 14, navy, true);
        const QString detail=QStringLiteral("到达 %1 · %2%3").arg(minutes(item.job.submittedMs),
            item.job.paper == cps::PaperSize::A3 ? QStringLiteral("A3") : QStringLiteral("A4"),
            item.job.color ? QStringLiteral(" 彩色") : QStringLiteral(" 黑白"));
        d.text(78,card.y()+24,190,21,detail,11,slate);
        d.text(219, card.y(), 55, 25,
               QStringLiteral("%1 面").arg(item.job.pages * item.job.copies), 12, navy,
               false, false, Qt::AlignRight | Qt::AlignVCenter);
        d.hit(card, QStringLiteral("dispatch-order"), item.id,
              item.id + QStringLiteral(" ") + item.title);
    }
    if (demo.orders.size() > 6) {
        d.pageButton({35, 714, 67, 34}, QStringLiteral("上页"), "dispatch-page", "-1", offset > 0);
        d.text(108, 714, 106, 34, QStringLiteral("%1 / %2").arg(offset / 6 + 1).arg((demo.orders.size() + 5) / 6),
            13, slate, false, false, Qt::AlignCenter);
        d.pageButton({219, 714, 67, 34}, QStringLiteral("下页"), "dispatch-page", "1", offset + 6 < demo.orders.size());
    }
}

void comparison(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel(QRectF(314, 293, 1254, 480), Qt::white, ruleColor, 4);
    d.text(333, 310, 730, 35, QStringLiteral("FCFS、SJF 与全局优化求解"), 22, navy, true);
    if (demo.stress) d.button({1090, 308, 213, 35}, QStringLiteral("更新求解报告"), "dispatch-reload");
    d.text(1350, 315, 102, 29, QStringLiteral("平均等待"), 16, slate,
           false, false, Qt::AlignCenter);
    d.text(1467, 315, 87, 29, QStringLiteral("综合分"), 16, slate,
           false, false, Qt::AlignCenter);
    d.text(1329, 348, 224, 21, QStringLiteral("全部接受订单口径"), 13, slate,
           false, false, Qt::AlignCenter);
    d.p.setPen(QPen(ruleColor, 1));
    d.p.drawLine(QPointF(1337, 331), QPointF(1337, timelineBottom));

    const cps::TimeMs horizon = std::max<cps::TimeMs>(1, demo.horizonMs);
    const auto xAt = [horizon](cps::TimeMs time) {
        return timelineLeft + timelineWidth * std::clamp(static_cast<double>(time) / horizon, 0., 1.);
    };
    for (int row = 0; row < static_cast<int>(demo.runs.size()); ++row) {
        if (row == selection.policy) {
            d.panel(QRectF(326, rowY[row] - 22, 1228, 104), QColor("#FFF0E9"), Qt::transparent, 4);
            d.p.setPen(Qt::NoPen); d.p.setBrush(red);
            d.p.drawRoundedRect(QRectF(326, rowY[row] - 22, 5, 104), 2.5, 2.5);
            d.p.setBrush(Qt::NoBrush);
        }
    }
    for (int tick = 0; tick <= 8; ++tick) {
        const auto time = horizon * tick / 8;
        const qreal x = xAt(time);
        d.p.setPen(QPen(QColor("#DBDFE5"), 1, Qt::DashLine));
        d.p.drawLine(QPointF(x, 380), QPointF(x, timelineBottom));
        d.text(x - 35, 349, 70, 29, minutes(time, 0), 14, slate, false, false, Qt::AlignCenter);
    }
    d.text(1285,349,47,29,QStringLiteral("分钟"),13,slate);

    for (int row = 0; row < static_cast<int>(demo.runs.size()); ++row) {
        const auto& run = demo.runs[static_cast<std::size_t>(row)];
        const qreal y = rowY[row];
        d.text(341, y - 12, 143, 66, QString(run.label).replace(QStringLiteral(" · "), "\n"), 15, navy, true,
            false, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
        d.hit(QRectF(337, y - 22, 153, 104), QStringLiteral("dispatch-policy"), QString::number(row), run.label);
        // Keep name / metric controls outside the bar hit regions.
        d.hit(QRectF(1338, y - 22, 216, 104), QStringLiteral("dispatch-policy"), QString::number(row), run.label);
        const int lanes = std::max(1, int(demo.printers.size()));
        const qreal laneHeight = std::min<qreal>(24, 82. / lanes);
        const qreal trackTop = lanes == 1 ? y : y - 12;
        for (int m = 0; m < lanes; ++m) {
            const auto laneY = trackTop + m * laneHeight;
            d.text(486, laneY, 20, laneHeight, QString::number(m + 1), 9, slate, false, false, Qt::AlignCenter);
            d.p.setPen(QPen(QColor("#ECEEF1"), .7));
            d.p.drawLine(QPointF(timelineLeft, laneY + laneHeight), QPointF(timelineLeft + timelineWidth, laneY + laneHeight));
        }
        for (const auto& dispatch : run.result.dispatches) {
            const auto* item = orderFor(demo, dispatch.jobId);
            if (!item) continue;
            const qreal left = xAt(dispatch.startedMs);
            const qreal width = std::max<qreal>(1.6, xAt(dispatch.finishedMs) - left);
            const qreal barY = trackTop + dispatch.printer * laneHeight;
            const qreal barHeight = lanes == 1 ? 46 : laneHeight - 1;
            const QRectF bar(left, barY, width, barHeight);
            const bool focus = item->id == selection.order;
            d.panel(bar.adjusted(.5, .5, -.5, -.5), item->color,
                    focus ? red : QColor(Qt::white), 3);
            if (focus) {
                d.p.setPen(QPen(red, 2)); d.p.setBrush(Qt::NoBrush);
                d.p.drawRoundedRect(bar.adjusted(1, 1, -1, -1), 3, 3);
            }
            d.p.save(); d.p.setClipRect(bar.adjusted(1, 1, -1, -1));
            if (width >= (lanes == 1 ? 18 : 27)) d.text(left + 1, barY, width - 2, barHeight,
                item->id, lanes == 1 ? 15 : 9, navy, false, false, Qt::AlignCenter);
            d.p.restore();
            d.hit(bar, QStringLiteral("dispatch-order"), item->id,
                  QStringLiteral("%1 · %2 · 设备 %3 · %4–%5 分钟").arg(run.label, item->id)
                      .arg(dispatch.printer + 1).arg(minutes(dispatch.startedMs), minutes(dispatch.finishedMs)));
        }
        d.text(1349, y, 104, 46, QString::number(run.averageWaitMinutes, 'f', 1) + QStringLiteral(" min"),
               18, navy, false, false, Qt::AlignCenter);
        d.text(1468, y, 85, 46, demo.orders.isEmpty() ? QStringLiteral("—") : QString::number(run.quality.overallScore, 'f', 2),
               18, navy, false, false, Qt::AlignCenter);
        d.text(1345, y + 49, 208, 25, QStringLiteral("快速准时 %1/%2 · 完成 %3").arg(run.quality.expressOnTime)
            .arg(run.quality.expressAccepted).arg(run.result.business.completed), 12, slate, false, false, Qt::AlignCenter);
    }

    const qreal cursor = xAt(selection.nowMs);
    d.p.save(); d.p.setPen(QPen(red, 2.5, Qt::SolidLine, Qt::RoundCap));
    d.p.drawLine(QPointF(cursor, 380), QPointF(cursor, timelineBottom));
    d.p.setPen(Qt::NoPen); d.p.setBrush(red);
    QPainterPath marker; marker.moveTo(cursor - 7, 371); marker.lineTo(cursor + 7, 371);
    marker.lineTo(cursor, 382); marker.closeSubpath(); d.p.drawPath(marker); d.p.restore();
}

void explanation(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel(QRectF(18, 786, 1550, 141), Qt::white, ruleColor, 4);
    d.icon(QStringLiteral("document"), 40, 812, 44);
    d.text(106, 802, 749, 37, QStringLiteral("排程依据与订单详情"), 22, navy, true);
    const auto& run = demo.runs[static_cast<std::size_t>(std::clamp(selection.policy, 0, 2))];
    std::vector<const cps::Dispatch*> sequence;
    for (const auto& dispatch : run.result.dispatches) sequence.push_back(&dispatch);
    std::stable_sort(sequence.begin(), sequence.end(), [](const auto* a, const auto* b) {
        return a->startedMs < b->startedMs;
    });
    QStringList completed;
    QStringList running;
    const cps::Dispatch* next = nullptr;
    for (const auto* dispatch : sequence) {
        if (dispatch->finishedMs <= selection.nowMs) completed << QString::fromStdString(dispatch->jobId);
        else if (dispatch->startedMs <= selection.nowMs) running << QString::fromStdString(dispatch->jobId);
        else if (!next) next = dispatch;
    }
    const QString badgeText = run.label + QStringLiteral("：");
    const qreal badgeWidth = QFontMetricsF(merchantFont(16, true)).horizontalAdvance(badgeText) + 20;
    d.panel(QRectF(106, 847, badgeWidth, 30), QColor("#FDE8DF"), Qt::transparent, 4);
    d.text(115, 847, badgeWidth - 18, 30, badgeText, 16, navy, true);
    const QString policyExplanation = run.explanation.section(QStringLiteral(" 无取件期限"), 0, 0);
    d.p.setFont(merchantFont(14)); d.p.setPen(slate);
    d.p.drawText(QRectF(116 + badgeWidth, 842, 770 - badgeWidth, 44),
                 Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, policyExplanation);
    QString status = QStringLiteral("%1 min · 已完成 %2/%3 单").arg(minutes(selection.nowMs))
        .arg(completed.size()).arg(demo.orders.size());
    if (!running.isEmpty()) {
        status += QStringLiteral(" · %1 台正在打印").arg(running.size());
    } else if (!next) {
        status += QStringLiteral(" · 本批已结束");
    } else {
        status += QStringLiteral(" · 设备空闲");
    }
    if (next) {
        const auto* item = orderFor(demo, next->jobId);
        status += QStringLiteral(" · 下一单 %1（%2 面，%3 min）")
            .arg(QString::fromStdString(next->jobId)).arg(item ? item->job.pages * item->job.copies : 0)
            .arg(minutes(next->startedMs));
    }
    d.text(106, 886, 781, 29, status, 14, slate);

    const QString automaticFocus = next ? QString::fromStdString(next->jobId) : running.value(0);
    const QString focus = selection.order.isEmpty() ? automaticFocus : selection.order;
    const auto* item = orderFor(demo, focus.toStdString());
    if (item) {
        d.panel({928, 808, 38, 48}, item->color, item->color, 4);
        d.text(982, 804, 557, 31, item->id + QStringLiteral(" · ") + item->title, 18, navy, true);
        d.text(928, 859, 620, 24, QStringLiteral("%1 面 · %2 · %3 · 期限 %4 min").arg(item->job.sheets())
            .arg(item->job.paper == cps::PaperSize::A3 ? "A3" : "A4", item->job.color ? QStringLiteral("彩色") : QStringLiteral("黑白"))
            .arg(item->job.deadlineMs ? minutes(item->job.deadlineMs) : QStringLiteral("无")), 14, slate);
        const auto found = std::find_if(sequence.begin(), sequence.end(), [&item](const auto* dispatch) { return dispatch->jobId == item->job.id; });
        d.text(928, 888, 620, 25, found == sequence.end() ? QStringLiteral("观察窗口内仍在排队") :
            QStringLiteral("设备 %1 · %2–%3 min · 等待 %4 min").arg((*found)->printer + 1)
                .arg(minutes((*found)->startedMs), minutes((*found)->finishedMs), minutes((*found)->startedMs - item->job.submittedMs)), 14, slate);
    } else if (demo.orders.isEmpty()) {
        d.text(928, 804, 620, 106, demo.notice, 13, slate, false, false, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
    }
}
} // namespace

void paintDispatch(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.p.save();
    // The window paints the page fill and the full-width paper banner behind this body.
    d.p.setClipRect(QRectF(0, 56, 1586, 884));
    d.headline(142, 84, 84, QStringLiteral("同一批订单，三种"), QStringLiteral("安排。"));
    d.text(142, 165, 1050, 32,
           QStringLiteral("看见顺序，也看见取舍。  %1").arg(demo.scenarioName), 22, slate);
    toolbar(d, demo, selection.playing);
    orders(d, demo, selection);
    comparison(d, demo, selection);
    if (demo.orders.isEmpty()) {
        d.panel({493, 385, 802, 361}, QColor("#FAFAF9"), ruleColor, 4);
        d.text(526, 420, 738, 243, demo.notice, 17, slate, false, false, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
        d.button({526, 680, 174, 40}, QStringLiteral("重新读取报告"), "dispatch-reload");
    }
    explanation(d, demo, selection);
    d.p.restore();
}
} // namespace cps::gui::merchant
