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

void toolbar(Painter& d, bool playing, bool business, bool classroom) {
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
    control(d, QRectF(908, 231, 162, 47), playing ? QStringLiteral("暂停演示") : classroom ? QStringLiteral("播放推演") : QStringLiteral("播放演示"),
            QStringLiteral("dispatch-play"), playing ? QStringLiteral("pause") : QStringLiteral("play"), true);
    d.text(1094, 230, 454, 24, classroom ? QStringLiteral("扫码提交 → 共同后台 → 现场工作台") : business ? QStringLiteral("六台异构设备 · 8 小时营业 · 非抢占") : QStringLiteral("单台教学打印机 · 3 页/分钟 · 非抢占"), 13, slate);
    d.text(1094, 254, 454, 24, classroom ? QStringLiteral("实时跟随原速 · 播放推演 1 秒 = 4 分钟") : business ? QStringLiteral("1×：1 秒 = 4 分钟 · 指标为闭店结果") : QStringLiteral("准时完成包含无取件期限的订单"), 13, slate);
}

void orders(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel(QRectF(18, 293, 286, 480), Qt::white, ruleColor, 4);
    d.text(35, 310, 148, 35, QStringLiteral("这一批订单"), 22, navy, true);
    int pages = 0;
    for (const auto& item : demo.orders) pages += item.job.pages * item.job.copies;
    d.text(181, 311, 106, 34,
           QStringLiteral("共 %1 单 · %2 页").arg(demo.orders.size()).arg(pages), 14, slate,
           false, false, Qt::AlignRight | Qt::AlignVCenter);
    for (qsizetype i = 0; i < demo.orders.size() && i < 6; ++i) {
        const auto& item = demo.orders[i];
        const QRectF card(34, 362 + i * 66, 255, 58);
        const bool focus = item.id == selection.order;
        d.panel(card, focus ? QColor("#FFF8F4") : QColor(Qt::white), focus ? red : ruleColor, 4);
        d.panel(QRectF(49, card.y() + 12, 27, 34), item.color, item.color, 4);
        d.text(92, card.y()+3, 115, 29, item.id + QStringLiteral("  ") + item.title, 18, navy, true);
        QString detail=QStringLiteral("到达 %1 · 期限 %2").arg(minutes(item.job.submittedMs),item.job.deadlineMs?minutes(item.job.deadlineMs):QStringLiteral("无"));
        if(selection.policy==2) {
            const auto tier = item.job.service == cps::ServiceTier::Express ? QStringLiteral("快速") :
                item.job.service == cps::ServiceTier::Standard ? QStringLiteral("普通") : QStringLiteral("灵活");
            detail = QStringLiteral("%1 · %2 · 到达 %3").arg(tier, money(item.job.quotedCents), minutes(item.job.submittedMs));
        }
        d.text(92,card.y()+32,181,22,detail,11,slate);
        d.text(211, card.y(), 63, 58,
               QStringLiteral("%1 页").arg(item.job.pages * item.job.copies), 16, navy,
               false, false, Qt::AlignRight | Qt::AlignVCenter);
        d.hit(card, QStringLiteral("dispatch-order"), item.id,
              item.id + QStringLiteral(" ") + item.title);
    }
}

void comparison(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel(QRectF(314, 293, 1254, 480), Qt::white, ruleColor, 4);
    d.text(333, 310, 416, 35, QStringLiteral("三种策略，两层调度"), 22, navy, true);
    d.text(1350, 315, 102, 29, QStringLiteral("平均等待"), 16, slate,
           false, false, Qt::AlignCenter);
    d.text(1467, 315, 87, 29, QStringLiteral("准时完成"), 16, slate,
           false, false, Qt::AlignCenter);
    d.text(1329, 348, 224, 21, QStringLiteral("整批结果"), 13, slate,
           false, false, Qt::AlignCenter);
    d.p.setPen(QPen(ruleColor, 1));
    d.p.drawLine(QPointF(1337, 331), QPointF(1337, timelineBottom));

    const cps::TimeMs horizon = std::max<cps::TimeMs>(1, demo.horizonMs);
    const auto xAt = [horizon](cps::TimeMs time) {
        return timelineLeft + timelineWidth * std::clamp(static_cast<double>(time) / horizon, 0., 1.);
    };
    for (int row = 0; row < static_cast<int>(demo.runs.size()); ++row) {
        if (row == selection.policy) {
            d.panel(QRectF(326, rowY[row] - 11, 1228, 67), QColor("#FFF0E9"), Qt::transparent, 4);
            d.p.setPen(Qt::NoPen); d.p.setBrush(red);
            d.p.drawRoundedRect(QRectF(326, rowY[row] - 11, 5, 67), 2.5, 2.5);
            d.p.setBrush(Qt::NoBrush);
        }
    }
    const int lastMinute=static_cast<int>(horizon/60000);
    for (int tick = 0; tick <= lastMinute; ++tick) {
        const qreal x = xAt(static_cast<cps::TimeMs>(tick)*60000);
        d.p.setPen(QPen(QColor("#DBDFE5"), 1, Qt::DashLine));
        d.p.drawLine(QPointF(x, 380), QPointF(x, timelineBottom));
        if (tick % 2 == 0) {
            const QString value = QString::number(tick);
            d.text(x - 35, 349, 70, 29, value, 16, slate, false, false, Qt::AlignCenter);
        }
    }
    d.text(1285,349,47,29,QStringLiteral("分钟"),13,slate);

    for (int row = 0; row < static_cast<int>(demo.runs.size()); ++row) {
        const auto& run = demo.runs[static_cast<std::size_t>(row)];
        const qreal y = rowY[row];
        d.text(349, y, 153, 46, run.label, 15, navy, true);
        d.hit(QRectF(337, y - 11, 168, 67), QStringLiteral("dispatch-policy"), QString::number(row), run.label);
        // Keep name / metric controls outside the bar hit regions.
        d.hit(QRectF(1291, y - 11, 263, 67), QStringLiteral("dispatch-policy"), QString::number(row), run.label);
        for (const auto& dispatch : run.result.dispatches) {
            const auto* item = orderFor(demo, dispatch.jobId);
            if (!item) continue;
            const qreal left = xAt(dispatch.startedMs);
            const qreal width = xAt(dispatch.finishedMs) - left;
            if (width <= 0) continue;
            const QRectF bar(left, y, width, 46);
            const bool focus = item->id == selection.order;
            d.panel(bar.adjusted(.5, .5, -.5, -.5), item->color,
                    focus ? red : QColor(Qt::white), 3);
            if (focus) {
                d.p.setPen(QPen(red, 2)); d.p.setBrush(Qt::NoBrush);
                d.p.drawRoundedRect(bar.adjusted(1, 1, -1, -1), 3, 3);
            }
            d.p.save(); d.p.setClipRect(bar.adjusted(1, 1, -1, -1));
            if (width >= 28) {
                d.text(left + 1, y + 2, width - 2, 23, item->id, 16, navy, false, false, Qt::AlignCenter);
                const QString duration = minutes(dispatch.finishedMs - dispatch.startedMs);
                d.text(left + 1, y + 23, width - 2, 20, duration, width < 42 ? 13 : 14,
                       navy, false, false, Qt::AlignCenter);
            } else {
                d.text(left + 1, y, width - 2, 46, item->id, 13, navy, false, false, Qt::AlignCenter);
            }
            d.p.restore();
            d.hit(bar, QStringLiteral("dispatch-order"), item->id,
                  QStringLiteral("%1 · %2 · %3–%4 分钟").arg(run.label, item->id,
                      minutes(dispatch.startedMs), minutes(dispatch.finishedMs)));
        }
        d.text(1349, y, 104, 46, QString::number(run.averageWaitMinutes, 'f', 1) + QStringLiteral(" min"),
               18, navy, false, false, Qt::AlignCenter);
        d.text(1468, y, 85, 46, QStringLiteral("%1 / %2").arg(run.onTime).arg(demo.orders.size()),
               18, navy, false, false, Qt::AlignCenter);
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
    d.text(106, 802, 749, 37, QStringLiteral("为什么下一单是它？"), 22, navy, true);
    const auto& run = demo.runs[static_cast<std::size_t>(std::clamp(selection.policy, 0, 2))];
    std::vector<const cps::Dispatch*> sequence;
    for (const auto& dispatch : run.result.dispatches) sequence.push_back(&dispatch);
    std::stable_sort(sequence.begin(), sequence.end(), [](const auto* a, const auto* b) {
        return a->startedMs < b->startedMs;
    });
    QStringList completed;
    const cps::Dispatch* running = nullptr;
    const cps::Dispatch* next = nullptr;
    for (const auto* dispatch : sequence) {
        if (dispatch->finishedMs <= selection.nowMs) completed << QString::fromStdString(dispatch->jobId);
        else if (dispatch->startedMs <= selection.nowMs) running = dispatch;
        else if (!next) next = dispatch;
    }
    const QString badgeText = run.label + QStringLiteral("：");
    const qreal badgeWidth = QFontMetricsF(merchantFont(16, true)).horizontalAdvance(badgeText) + 20;
    d.panel(QRectF(106, 847, badgeWidth, 30), QColor("#FDE8DF"), Qt::transparent, 4);
    d.text(115, 847, badgeWidth - 18, 30, badgeText, 16, navy, true);
    // The shared clock / metric definitions are visible in the toolbar. Keep
    // this paragraph focused on the selected algorithm, with wrapping rather
    // than dropping the end of the balanced scheduling rule.
    const QString policyExplanation = run.explanation.section(QStringLiteral(" 无取件期限"), 0, 0);
    d.p.setFont(merchantFont(14)); d.p.setPen(slate);
    d.p.drawText(QRectF(116 + badgeWidth, 842, 770 - badgeWidth, 44),
                 Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, policyExplanation);
    QString status = QStringLiteral("%1 min · 已完成 %2/%3 单").arg(minutes(selection.nowMs))
        .arg(completed.size()).arg(demo.orders.size());
    if (running) {
        status += QStringLiteral(" · 正在打印 %1").arg(QString::fromStdString(running->jobId));
    } else if (!next) {
        status += QStringLiteral(" · 本批已结束");
    } else {
        status += QStringLiteral(" · 设备空闲");
    }
    if (next) {
        const auto* item = orderFor(demo, next->jobId);
        status += QStringLiteral(" · 下一单 %1（%2 页，%3 min 开始）")
            .arg(QString::fromStdString(next->jobId)).arg(item ? item->job.pages * item->job.copies : 0)
            .arg(minutes(next->startedMs));
    }
    d.text(106, 886, 781, 29, status, 14, slate);

    const QString automaticFocus = next ? QString::fromStdString(next->jobId)
        : running ? QString::fromStdString(running->jobId) : QString{};
    const QString focus = selection.order.isEmpty() ? automaticFocus : selection.order;
    for (std::size_t i = 0; i < sequence.size() && i < 6; ++i) {
        const auto* item = orderFor(demo, sequence[i]->jobId);
        if (!item) continue;
        const QRectF tile(909 + static_cast<qreal>(i) * 112, 821, 68, 64);
        const bool active = item->id == focus;
        d.panel(tile, item->color, active ? red : item->color, 5);
        if (active) {
            d.p.setPen(QPen(red, 1.5)); d.p.setBrush(Qt::NoBrush);
            d.p.drawRoundedRect(tile.adjusted(-3, -3, 3, 3), 7, 7);
        }
        d.text(tile.x(), 827, tile.width(), 28, item->id, 18, navy, false, false, Qt::AlignCenter);
        d.text(tile.x(), 854, tile.width(), 25,
               QStringLiteral("%1 页").arg(item->job.pages * item->job.copies), 14, navy,
               false, false, Qt::AlignCenter);
        d.hit(tile, QStringLiteral("dispatch-order"), item->id, item->id + QStringLiteral(" ") + item->title);
        if (i + 1 < sequence.size() && i < 5) d.icon(QStringLiteral("arrow"), tile.right() + 16, 843, 17, slate);
    }
}

void businessOrders(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel({18, 293, 286, 515}, Qt::white, ruleColor, 6);
    d.text(35, 309, 148, 32, demo.classroom ? QStringLiteral("现场订单") : QStringLiteral("模拟订单"), 20, navy, true);
    d.text(185, 311, 100, 28, QStringLiteral("%1 单").arg(demo.orders.size()), 14, slate, false, false, Qt::AlignRight | Qt::AlignVCenter);
    d.text(35, 344, 250, 22, QStringLiteral("点击订单，定位打印时刻"), 12, slate);
    const int offset = std::clamp(selection.orderOffset, 0, std::max(0, int(demo.orders.size()) - 1));
    for (int i = 0; i < 6 && offset + i < demo.orders.size(); ++i) {
        const auto& item = demo.orders[offset + i];
        const auto& j = item.job;
        const QRectF card(34, 379 + i * 57, 255, 51);
        d.panel(card, item.id == selection.order ? QColor("#FFF0E9") : QColor(Qt::white), item.id == selection.order ? red : ruleColor, 4);
        d.panel({42, card.y() + 10, 5, 30}, item.color, item.color, 2);
        d.text(55, card.y() + 2, 225, 24, (item.label.isEmpty()?item.id:item.label) + "  " + item.title, 14, navy, true);
        const auto tier = j.service == cps::ServiceTier::Express ? QStringLiteral("快") : j.service == cps::ServiceTier::Standard ? QStringLiteral("普") : QStringLiteral("灵");
        const QString detail = QStringLiteral("%1页×%2份 · %3 · %4分到达").arg(j.pages).arg(j.copies).arg(tier, minutes(j.submittedMs));
        d.text(55, card.y() + 25, 225, 22, detail, 11, slate);
        d.hit(card, "dispatch-focus-order", item.id, QStringLiteral("定位 %1 %2 · %3 · 报价 %4 · 期限 %5 分钟")
            .arg(item.id, item.title, detail, money(j.quotedCents), minutes(j.deadlineMs)));
    }
    d.pageButton({34, 747, 84, 33}, QStringLiteral("上一页"), "dispatch-prev", {}, offset > 0);
    d.text(120, 747, 80, 33, QStringLiteral("%1 / %2").arg(offset / 6 + 1).arg((demo.orders.size() + 5) / 6), 13, slate, false, false, Qt::AlignCenter);
    d.pageButton({205, 747, 84, 33}, QStringLiteral("下一页"), "dispatch-next", {}, offset + 6 < demo.orders.size());
    if(demo.classroom&&demo.orders.isEmpty()){
        d.text(35, 435, 250, 35, QStringLiteral("等待同学扫码下单"), 17, navy, true);
        d.text(35, 478, 250, 28, QStringLiteral("选文件并确认下单后自动出现"), 12, slate);
    }
    d.text(35, 788, 240, 18, demo.classroom?QStringLiteral("真实上传订单 · 同批独立回放"):QStringLiteral("模拟业务数据 · 独立回放"), 11, slate);
}

void businessComparison(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel({314, 293, 1254, 515}, Qt::white, ruleColor, 6);
    const auto view = dispatchBusinessTimeline(demo, selection.nowMs);
    d.text(333, 307, 290, 32, demo.classroom?QStringLiteral("三种策略 · 现场对比"):QStringLiteral("三种策略 · 局部排程"), 18, navy, true);
    d.text(628, 310, 410, 28, QStringLiteral("30 分钟滚动窗口 · 每台最多 3 单"), 12, slate);
    d.text(1085, 307, 190, 32, demo.classroom?QStringLiteral("同批重放结果"):QStringLiteral("整场营业结果"), 15, slate);
    if(demo.classroom)d.button({1293,303,124,34},selection.following?QStringLiteral("暂停跟随"):QStringLiteral("实时跟随"),"dispatch-follow");
    d.button({1433, 303, 119, 34}, QStringLiteral("导出仿真"), "dispatch-export");
    const qreal left = 480, width = 568;
    const auto xAt = [&](cps::TimeMs time) { return left + width * std::clamp(double(time - view.startMs) / (view.endMs - view.startMs), 0., 1.); };
    for (auto time = view.startMs; time <= view.endMs; time += 5 * 60000) {
        const qreal x = xAt(time);
        d.p.setPen(QPen(ruleColor, 1, Qt::DashLine));
        d.p.drawLine(QPointF(x, 376), QPointF(x, 788));
        d.text(x - 28, 344, 56, 25, minutes(time, 0) + QStringLiteral("分"), 12, slate, false, false, Qt::AlignCenter);
    }
    d.p.setPen(QPen(ruleColor, 1));
    d.p.drawLine(QPointF(1065, 350), QPointF(1065, 788));
    const QStringList headers{QStringLiteral("平均等待 / 分"), QStringLiteral("P95 等待 / 分"), QStringLiteral("快速准时率"), QStringLiteral("综合得分")};
    for (int row = 0; row < 3; ++row) {
        const auto& run = demo.runs[static_cast<std::size_t>(row)];
        const qreal y = 376 + row * 140;
        if (selection.policy == row) {
            d.panel({326, y, 1228, 132}, QColor("#FFF5EF"), Qt::transparent, 5);
            d.panel({327, y + 6, 3, 120}, red, red, 1);
        }
        d.text(342, y + 1, 270, 24, run.label, 15, navy, true);
        d.hit({338, y, 274, 26}, "dispatch-policy", QString::number(row), run.label + " · " + run.explanation);
        for (int printer = 0; printer < int(demo.fleet.size()); ++printer) {
            const qreal lane = y + 34 + printer * 16;
            d.text(342, lane, 126, 14, QString::fromStdString(demo.fleet[static_cast<std::size_t>(printer)].name), 11, slate);
            d.p.fillRect(QRectF(left, lane, width, 13), QColor("#F3F5F7"));
        }
        for (const auto& dispatch : view.runs[static_cast<std::size_t>(row)]) {
            const auto* order = orderFor(demo, dispatch.jobId);
            if (!order || dispatch.startedMs >= demo.horizonMs) continue;
            const qreal x = xAt(dispatch.startedMs), barWidth = xAt(dispatch.finishedMs) - x;
            const QRectF bar(x, y + 34 + dispatch.printer * 16, barWidth, 13);
            const bool focus = order->id == selection.order;
            d.p.save();
            d.p.setOpacity(focus || dispatch.startedMs <= selection.nowMs ? 1. : .8);
            if (barWidth > 2) d.panel(bar.adjusted(.7, 0, -.7, 0), order->color, Qt::white, 2);
            else d.p.fillRect(bar, order->color);
            d.p.restore();
            if (focus) { d.p.setPen(QPen(red, 2)); d.p.setBrush(Qt::NoBrush); d.p.drawRect(bar); }
            if (barWidth >= 34) d.text(x + 2, bar.y(), barWidth - 4, bar.height(), order->label.isEmpty()?order->id:order->label, 10, navy, false, false, Qt::AlignCenter);
            if (barWidth >= 4) d.hit(bar, "dispatch-order", order->id, QStringLiteral("%1 · %2 %3 · 设备 %4 · %5–%6 分钟%7")
                .arg(run.label, order->id, order->title).arg(dispatch.printer + 1)
                .arg(minutes(dispatch.startedMs), minutes(dispatch.finishedMs), dispatch.finishedMs > demo.horizonMs ? QStringLiteral(" · 闭店未完成") : QString()));
        }
        const auto& q = run.quality;
        const QStringList values{QString::number(run.averageWaitMinutes, 'f', 1),
            QString::number(q.p95ObservedWaitMs / 60000., 'f', 1),
            QString::number(q.expressOnTimeRate * 100, 'f', 1) + "%", QString::number(q.overallScore, 'f', 2)};
        for (int col = 0; col < 4; ++col) {
            const qreal column = 1082 + col * 116;
            d.text(column, y + 10, 110, 22, headers[col], 12, slate, false, false, Qt::AlignCenter);
            d.text(column, y + 39, 110, 32, values[col], 21, col == 3 ? red : navy, col == 3, false, Qt::AlignCenter);
        }
        d.text(1090, y + 83, 178, 22, QStringLiteral("完成 %1 / %2 单").arg(run.result.business.completed).arg(demo.orders.size()), 13, navy);
        d.text(1280, y + 83, 260, 22, QStringLiteral("贡献利润  %1").arg(money(run.result.business.contributionCents)), 13, navy, false, false, Qt::AlignRight | Qt::AlignVCenter);
        d.text(1090, y + 108, 450, 19, QStringLiteral("设备利用率 %1%     贡献兑现率 %2%")
            .arg(run.result.performance.utilization * 100, 0, 'f', 1).arg(q.contributionScore * 100, 0, 'f', 1), 11, slate);
        d.hit({1080, y, 472, 132}, "dispatch-policy", QString::number(row), run.label + QStringLiteral(" · 快速准时 %1/%2；平均/P95 等待包含未开打订单；综合分为等待、快速履约、贡献兑现三项等权均值 × 100").arg(q.expressOnTime).arg(q.expressAccepted));
    }
    const qreal cursor = xAt(selection.nowMs);
    d.p.setPen(QPen(red, 1.5)); d.p.drawLine(QPointF(cursor, 376), QPointF(cursor, 788));
}

void businessExplanation(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.panel({18, 820, 1550, 107}, Qt::white, ruleColor, 6);
    d.text(1300, 829, 245, 26, QStringLiteral("当前 %1 / %2 分钟").arg(minutes(selection.nowMs),minutes(demo.horizonMs,0)), 12, slate, false, false, Qt::AlignRight | Qt::AlignVCenter);
    if (const auto* order = findDispatchOrder(demo, selection.order.toStdString())) {
        const auto& j = order->job;
        d.text(35, 829, 410, 27, (order->label.isEmpty()?order->id:order->label) + "  " + order->title, 18, navy, true);
        const auto detail = QStringLiteral("%1%2%3 · %4页×%5份 · %6 · 期限 %7 分")
            .arg(j.paper == cps::PaperSize::A3 ? "A3" : "A4", j.color ? QStringLiteral("彩色") : QStringLiteral("黑白"),
                 j.duplex ? QStringLiteral("双面") : QStringLiteral("单面")).arg(j.pages).arg(j.copies).arg(money(j.quotedCents), minutes(j.deadlineMs));
        d.text(460, 829, 810, 27, detail, 13, slate);
        for (int row = 0; row < 3; ++row) {
            const auto& run = demo.runs[static_cast<std::size_t>(row)];
            const qreal x = 35 + row * 505;
            if (row > 0) {d.p.setPen(QPen(ruleColor, 1));d.p.drawLine(QPointF(x - 18, 866), QPointF(x - 18, 915));}
            d.text(x, 862, 470, 23, run.label, 13, slate);
            const auto found = std::find_if(run.result.dispatches.begin(), run.result.dispatches.end(), [&](const auto& item) {return item.jobId == j.id;});
            const QString timing = found == run.result.dispatches.end() ? QStringLiteral("闭店前未开打") :
                QStringLiteral("设备 %1    %2–%3 分钟%4").arg(found->printer + 1).arg(minutes(found->startedMs), minutes(found->finishedMs), found->finishedMs > demo.horizonMs ? QStringLiteral(" · 闭店未完成") : QString());
            d.text(x, 890, 470, 26, timing, 14, navy);
        }
        return;
    }
    d.text(35, 829, 1120, 27, QStringLiteral("综合调度 · 相对基线的变化"), 18, navy, true);
    const std::array<qreal, 5> x{{35, 240, 465, 740, 1115}};
    const std::array<qreal, 5> widths{{175, 195, 245, 345, 420}};
    const QStringList headers{QStringLiteral("对比基线"), QStringLiteral("综合得分"), QStringLiteral("平均等待"), QStringLiteral("快速准时率"), QStringLiteral("贡献利润")};
    for (int col = 0; col < 5; ++col) d.text(x[col], 859, widths[col], 18, headers[col], 11, slate);
    const auto number = [](double value, int precision) {return (value >= 0 ? QStringLiteral("+") : QString()) + QString::number(value, 'f', precision);};
    const auto& balanced = demo.runs[2];
    for (int row = 0; row < 2; ++row) {
        const auto& base = demo.runs[static_cast<std::size_t>(row)];
        const QStringList values{row == 0 ? QStringLiteral("FCFS") : QStringLiteral("SJF"),
            number(balanced.quality.overallScore - base.quality.overallScore, 2) + QStringLiteral(" 分"),
            number(balanced.averageWaitMinutes - base.averageWaitMinutes, 1) + QStringLiteral(" 分钟"),
            number(100 * (balanced.quality.expressOnTimeRate - base.quality.expressOnTimeRate), 1) + QStringLiteral(" 个百分点"),
            number((balanced.result.business.contributionCents - base.result.business.contributionCents) / 100., 2) + QStringLiteral(" 元")};
        for (int col = 0; col < 5; ++col) d.text(x[col], 879 + row * 22, widths[col], 21, values[col], 14, navy, col == 0);
    }
}
} // namespace

void paintDispatch(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.p.save();
    d.p.setClipRect(QRectF(0, 56, 1586, 884));
    d.p.fillRect(QRectF(0, 56, 1586, 884), QColor("#FDFEFE"));
    if (!d.assets.dispatchBackground.isNull()) d.p.drawImage(QRectF(0, 56, 1586, 162), d.assets.dispatchBackground);
    else d.p.fillRect(QRectF(0, 56, 1586, 162), QColor("#F7F6F2"));
    d.headline(142, 84, 84, QStringLiteral("同一批订单，三种"), QStringLiteral("安排。"));
    d.text(142, 165, 1050, 32,
           QStringLiteral("看见顺序，也看见取舍。  %1").arg(demo.scenarioName), 22, slate);
    toolbar(d, selection.playing, demo.business, demo.classroom);
    if (demo.business) {
        businessOrders(d, demo, selection);
        businessComparison(d, demo, selection);
        businessExplanation(d, demo, selection);
    } else {
        orders(d, demo, selection);
        comparison(d, demo, selection);
        explanation(d, demo, selection);
    }
    d.p.restore();
}
} // namespace cps::gui::merchant
