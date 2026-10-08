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
constexpr std::array<qreal, 5> rowY{{392, 466, 540, 613, 687}};

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

void toolbar(Painter& d, bool playing) {
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
    d.text(1094, 230, 454, 24, QStringLiteral("单台教学打印机 · 3 页/分钟 · 非抢占"), 13, slate);
    d.text(1094, 254, 454, 24, QStringLiteral("准时完成包含无取件期限的订单"), 13, slate);
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
        if(selection.policy==2)detail=QStringLiteral("优先级 %1 · 到达 %2").arg(item.job.priority).arg(minutes(item.job.submittedMs));
        else if(selection.policy==4)detail=QStringLiteral("%1 /单 · %2 /分").arg(money(item.job.quotedCents),money(qRound64(item.job.quotedCents*3./item.job.sheets())));
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
    d.text(333, 310, 416, 35, QStringLiteral("五种策略，同屏对比"), 22, navy, true);
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
    for (int row = 0; row < 5; ++row) {
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

    for (int row = 0; row < 5; ++row) {
        const auto& run = demo.runs[static_cast<std::size_t>(row)];
        const qreal y = rowY[row];
        d.text(349, y, 153, 46, run.label, 18, navy, true);
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
    const auto& run = demo.runs[static_cast<std::size_t>(std::clamp(selection.policy, 0, 4))];
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
    // than dropping the end of the aging / revenue rule.
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
} // namespace

void paintDispatch(Painter& d, const DispatchDemo& demo, const DispatchDemoSelection& selection) {
    d.p.save();
    d.p.setClipRect(QRectF(0, 56, 1586, 884));
    d.p.fillRect(QRectF(0, 56, 1586, 884), QColor("#FDFEFE"));
    if (!d.assets.dispatchBackground.isNull()) d.p.drawImage(QRectF(0, 56, 1586, 162), d.assets.dispatchBackground);
    else d.p.fillRect(QRectF(0, 56, 1586, 162), QColor("#F7F6F2"));
    d.headline(142, 84, 84, QStringLiteral("同一批订单，五种"), QStringLiteral("安排。"));
    d.text(142, 165, 1050, 32,
           QStringLiteral("看见顺序，也看见取舍。  %1").arg(demo.scenarioName), 22, slate);
    toolbar(d, selection.playing);
    orders(d, demo, selection);
    comparison(d, demo, selection);
    explanation(d, demo, selection);
    d.p.restore();
}
} // namespace cps::gui::merchant
