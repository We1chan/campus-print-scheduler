#include "merchant_painter.hpp"
#include <QFontMetricsF>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace cps::gui::merchant {
namespace {
const QColor faded = slate;
const QColor track("#E0E4ED"), blue("#314F80");
QString number(int value) { return QString::number(value); }
QString supply(int value, const QString& unit) {
    return value < 0 ? QStringLiteral("未配置") : number(value) + unit;
}
QString deviceState(const Device& device) {
    if (!device.online) return QStringLiteral("离线");
    if (!device.enabled) return QStringLiteral("暂停");
    return device.busy ? QStringLiteral("打印中") : QStringLiteral("空闲");
}
double fraction(int value, int total) {
    return total > 0 ? std::clamp(static_cast<double>(value) / total, 0.0, 1.0) : 0.0;
}
QString percent(double value) { return QString::number(value * 100.0, 'f', 0) + QStringLiteral("%"); }
void vertical(Painter& d, qreal x, qreal y, qreal h) {
    d.p.save(); d.p.setPen(QPen(ruleColor, 1)); d.p.drawLine(QPointF(x, y), QPointF(x, y + h)); d.p.restore();
}
void section(Painter& d, qreal x, qreal y, const QString& label, qreal width = 450) {
    d.text(x, y, width, 45, label, 18, navy, true);
}
void smallArrow(Painter& d, qreal x, qreal y, QColor color = navy) {
    d.p.save(); d.p.setPen(QPen(color, 1.5));
    d.p.drawLine(QPointF(x, y), QPointF(x + 6, y + 6));
    d.p.drawLine(QPointF(x + 6, y + 6), QPointF(x, y + 12)); d.p.restore();
}
void supplyBar(Painter& d, QRectF rect, int value, int capacity, QColor color) {
    if (value < 0 || capacity <= 0) return;
    d.progress(rect, fraction(value, capacity), color);
}
void deviceCard(Painter& d, const Snapshot& state, const Device& device,
                QRectF card, bool selected) {
    const qreal x = card.x(), y = card.y(), w = card.width();
    const qreal extra = card.height() - 394;
    // Spend the reclaimed footer space on the device image and vertical rhythm,
    // while keeping the same interface type scale as the student client.
    const qreal taskY = y + 232 + extra;
    d.panel(card, QColor(255, 255, 255, 210), selected ? red : ruleColor, 7);
    if (selected) {
        d.p.save(); d.p.setBrush(Qt::NoBrush); d.p.setPen(QPen(red, 1.7));
        d.p.drawRoundedRect(card, 7, 7); d.p.restore();
    }
    const qreal nameWidth=std::min(QFontMetricsF(merchantFont(18,true)).horizontalAdvance(device.name)+2,w-164);
    d.text(x+22,y+12,nameWidth,40,device.name,18,navy,true);
    // Optical centering compensates for the 14px status beside an 18px name.
    d.dot(x+22+nameWidth+18,y+31.5,deviceState(device),device.online&&device.enabled?green:slate);
    d.hit(card.adjusted(3,3,-3,-3),QStringLiteral("select-device"),number(device.index),device.name);
    const QRectF imageArea(x + (w - 360) / 2, y + 58, 360, 174 + extra);
    QSizeF imageSize=device.color ? QSizeF(251,185) : QSizeF(297,179);
    imageSize.scale(imageArea.size(),Qt::KeepAspectRatio);
    const QRectF image(imageArea.center()-QPointF(imageSize.width()/2,imageSize.height()/2),imageSize);
    d.art(device.color ? QStringLiteral("printer-color") : QStringLiteral("printer-black"), image);

    if (device.color) {
        d.text(x + 21, taskY - 2, w - 145, 29,
               device.busy ? QStringLiteral("当前任务  ") + device.jobId :
               device.enabled && device.online ? QStringLiteral("准备就绪，可接收打印任务") : QStringLiteral("设备暂不可接单"), 14, navy, true);
        vertical(d, x + w - 117, taskY + 5, 18);
        d.text(x + w - 96, taskY - 2, 82, 29, QStringLiteral("队列 %1 单").arg(device.queued), 14, faded);
        d.rule(x + 21, y + 264 + extra, w - 43);
        d.text(x + 21, y + 270 + extra, 110, 30, QStringLiteral("A4 纸张"), 14, navy, true);
        d.text(x + 155, y + 270 + extra, 180, 30,
               device.paper < 0 ? QStringLiteral("未配置") : QStringLiteral("%1 / %2 张").arg(device.paper).arg(device.capacity), 14, faded);
        supplyBar(d, {x + 21, y + 300 + extra, w - 43, 12}, device.paper, device.capacity, blue);
        const std::array<QColor, 4> ink{{navy, QColor("#009CDE"), QColor("#D00067"), QColor("#FFBA00")}};
        const QStringList labels{QStringLiteral("黑"), QStringLiteral("青"), QStringLiteral("品红"), QStringLiteral("黄")};
        const qreal spacing = (w - 43) / 4;
        for (int i = 0; i < 4; ++i) {
            const qreal tx = x + 21 + spacing * i;
            d.p.save(); d.p.setPen(Qt::NoPen); d.p.setBrush(ink[static_cast<std::size_t>(i)]);
            d.p.drawEllipse(QPointF(tx + 6, y + 346 + extra), 6, 6); d.p.restore();
            d.text(tx + 17, y + 331 + extra, 49, 30, labels[i], 14);
            d.text(tx + 54, y + 331 + extra, spacing - 64, 30, supply(device.toner[static_cast<std::size_t>(i)], QStringLiteral("%")), 12, faded, false, false, Qt::AlignRight | Qt::AlignVCenter);
            supplyBar(d, {tx, y + 362 + extra, spacing - 30, 12}, device.toner[static_cast<std::size_t>(i)], 100, ink[static_cast<std::size_t>(i)]);
        }
    } else {
        d.text(x + 22, taskY, 87, 30, QStringLiteral("当前任务"), 14, navy, true);
        d.text(x + 103, taskY, 149, 30, device.jobId.isEmpty() ? QStringLiteral("暂无") : device.jobId, 14);
        d.text(x + 309, taskY, 102, 30, QStringLiteral("%1 / %2 页").arg(device.printed).arg(device.total), 14, faded);
        vertical(d, x + w - 111, taskY + 9, 46);
        d.text(x + w - 91, taskY, 82, 30, QStringLiteral("队列 %1 单").arg(device.queued), 14, faded);
        const double ratio = fraction(device.printed, device.total);
        d.progress({x + 22, y + 273 + extra, w - 203, 14}, ratio, blue);
        d.text(x + w - 173, y + 263 + extra, 54, 31, percent(ratio), 14, faded);
        d.rule(x + 22, y + 302 + extra, w - 44);
        const qreal half = (w - 64) / 2;
        const qreal supplyDividerX = x + half + 20;
        const qreal paperValueX = x + 109;
        const qreal paperValueWidth = supplyDividerX - 16 - paperValueX;
        const QString paperValue = device.paper < 0 ? QStringLiteral("未配置")
            : QStringLiteral("%1 / %2 张").arg(device.paper).arg(device.capacity);
        QFont paperValueFont=merchantFont(14);
        // Keep the complete inventory value inside the gap before the divider.
        while (paperValueFont.pixelSize() > 1
               && QFontMetricsF(paperValueFont).horizontalAdvance(paperValue) > paperValueWidth)
            paperValueFont.setPixelSize(paperValueFont.pixelSize() - 1);
        d.text(x + 22, y + 313 + extra, 90, 31, QStringLiteral("A4 纸张"), 14, navy, true);
        d.text(paperValueX, y + 313 + extra, paperValueWidth, 31,
               paperValue, paperValueFont.pixelSize(), faded, false, false, Qt::AlignRight | Qt::AlignVCenter);
        supplyBar(d, {x + 22, y + 347 + extra, half - 24, 13}, device.paper, device.capacity, blue);
        vertical(d, supplyDividerX, y + 318 + extra, 55);
        d.text(x + half + 50, y + 313 + extra, 120, 31, QStringLiteral("黑色墨粉"), 14, navy, true);
        const int toner = device.toner[0];
        d.text(x + w - 108, y + 313 + extra, 84, 31, supply(toner, QStringLiteral("%")), 14, toner >= 0 && toner < 20 ? red : faded, false, false, Qt::AlignRight | Qt::AlignVCenter);
        supplyBar(d, {x + half + 50, y + 347 + extra, half - 12, 13}, toner, 100, toner >= 0 && toner < 20 ? red : navy);
        if (toner >= 0 && toner < 20) d.text(x + w - 146, y + 362 + extra, 122, 27, QStringLiteral("建议准备耗材"), 14, red, true, false, Qt::AlignRight | Qt::AlignVCenter);
    }
    Q_UNUSED(state);
}
void drawToggle(Painter& d, QRectF rect, bool on, const QString& value) {
    d.panel(rect, on ? navy : track, on ? navy : track, rect.height() / 2);
    d.p.save(); d.p.setPen(Qt::NoPen); d.p.setBrush(Qt::white);
    const qreal radius = rect.height() / 2 - 4;
    d.p.drawEllipse(QPointF(on ? rect.right() - radius - 4 : rect.left() + radius + 4, rect.center().y()), radius, radius); d.p.restore();
    d.hit({rect.x() - 5, rect.y() - 8, 94, rect.height() + 16}, QStringLiteral("toggle-device"), value, QStringLiteral("切换自动接单"));
}
void deviceDetails(Painter& d, const Snapshot& state, const Device* device) {
    section(d, 1175, 80, QStringLiteral("设备详情"), 350);
    if (!device) {
        d.icon(QStringLiteral("printer"), 1180, 152, 40);
        d.text(1233, 148, 297, 46, QStringLiteral("尚未配置打印设备"), 16, navy, true);
        d.text(1177, 239, 355, 64, QStringLiteral("添加设备后，可在这里查看配置与任务。"), 14, faded);
        return;
    }
    const QString id = number(device->index);
    d.icon(QStringLiteral("printer"), 1180, 149, 42);
    d.text(1236, 151, 211, 40, device->name, 18, navy, true);
    d.dot(1448, 172, device->online ? QStringLiteral("设备在线") : QStringLiteral("设备离线"), device->online ? green : slate);
    d.rule(1177, 215, 376);
    d.text(1177, 228, 250, 36, QStringLiteral("设备配置"), 16, navy, true);
    d.detailRow(272, QStringLiteral("支持纸张"), device->a3 ? QStringLiteral("A4 / A3") : QStringLiteral("A4"));
    d.detailRow(312, QStringLiteral("打印色彩"), device->color ? QStringLiteral("黑白 / 彩色") : QStringLiteral("黑白"));
    d.detailRow(352, QStringLiteral("双面打印"), device->duplex ? QStringLiteral("支持") : QStringLiteral("不支持"));
    d.detailRow(393, QStringLiteral("默认纸盒"), device->paper < 0 ? QStringLiteral("未配置") : QStringLiteral("纸盒 1 · A4"));
    d.text(1177, 429, 175, 35, QStringLiteral("自动接单"), 14, faded);
    drawToggle(d, {1464, 434, 45, 25}, device->enabled, id);
    d.text(1516, 430, 40, 34, device->enabled ? QStringLiteral("开启") : QStringLiteral("关闭"), 14);
    d.rule(1177, 486, 376);
    d.text(1177, 503, 270, 40, QStringLiteral("当前任务"), 16, navy, true);
    QString title;
    for (const Order& order : state.orders) if (order.id == device->jobId) { title = order.title; break; }
    const QString task = device->jobId.isEmpty() ? QStringLiteral("暂无打印任务") : device->jobId + (title.isEmpty() ? QString() : QStringLiteral(" · ") + title);
    d.text(1177, 546, 292, 35, task, 14);
    d.text(1470, 547, 82, 34, QStringLiteral("%1 / %2 页").arg(device->printed).arg(device->total), 14, navy, false, false, Qt::AlignRight | Qt::AlignVCenter);
    const double ratio = fraction(device->printed, device->total);
    d.progress({1177, 587, 333, 14}, ratio, blue);
    d.text(1519, 578, 43, 33, percent(ratio), 12);
    d.rule(1177, 636, 376);
    d.text(1177, 650, 280, 40, QStringLiteral("维护记录"), 16, navy, true);
    if (state.showcase) {
        d.text(1177, 699, 94, 35, QStringLiteral("10月1日"), 14, faded);
        d.text(1275, 699, 270, 35, QStringLiteral("清洁进纸滚轮"), 14, faded);
        d.rule(1177, 734, 376);
        d.text(1177, 740, 94, 35, QStringLiteral("9月28日"), 14, faded);
        d.text(1275, 740, 270, 35, QStringLiteral("更换黑色墨粉"), 14, faded);
    } else {
        d.text(1177, 699, 368, 64, QStringLiteral("暂无维护记录"), 14, faded);
    }
    d.rule(1177, 798, 376);
    d.text(1177, 807, 376, 35,
           device->paper < 0 || device->toner[0] < 0 ? QStringLiteral("尚未接入设备耗材遥测。") : QStringLiteral("耗材充足时，设备可持续接单。"), 14, faded);
    d.button({1177, 872, 178, 48}, QStringLiteral("设备设置"), QStringLiteral("device-settings"), id);
    d.button({1371, 872, 182, 48}, QStringLiteral("打印测试页"), QStringLiteral("test-page"), id, true);
}
QPainterPath receiptPath(QRectF rect) {
    QPainterPath path;
    path.moveTo(rect.left(), rect.top() + 2);
    for (int i = 1; i <= 60; ++i) {
        const qreal x = rect.left() + rect.width() * i / 60;
        path.lineTo(x, rect.top() + ((i * 19) % 5) * 0.6);
    }
    path.lineTo(rect.right(), rect.bottom() - 5);
    for (int i = 0; i <= 42; ++i) {
        const qreal x = rect.right() - rect.width() * i / 42;
        path.lineTo(x, rect.bottom() - (i % 2 == 0 ? 0 : 4 + (i % 3)));
    }
    path.lineTo(rect.left(), rect.top() + 2); path.closeSubpath(); return path;
}
void dashed(Painter& d, qreal x, qreal y, qreal width) {
    d.p.save(); QPen pen(QColor("#A9AFBC"), 1); pen.setDashPattern({5, 4});
    d.p.setPen(pen); d.p.drawLine(QPointF(x, y), QPointF(x + width, y)); d.p.restore();
}
void receiptRow(Painter& d, qreal y, const QString& label, const QString& value, bool muted = false) {
    d.text(1217, y, 179, 31, label, 16, muted ? faded : navy);
    d.text(1385, y, 119, 31, value, 16, navy, !muted, false, Qt::AlignRight | Qt::AlignVCenter);
}
void receipt(Painter& d, const Snapshot& state) {
    section(d, 1175, 80, QStringLiteral("营业摘要"), 365);
    const QPainterPath path = receiptPath({1190, 143, 338, 516});
    d.p.save(); d.p.setPen(Qt::NoPen);
    for (int i = 15; i >= 1; --i) {
        QTransform transform; transform.translate(i * 0.42, i * 0.78);
        d.p.setBrush(QColor(18, 27, 42, 3)); d.p.drawPath(transform.map(path));
    }
    d.p.setBrush(Qt::white); d.p.drawPath(path);
    d.p.setClipPath(path);
    if (!d.assets.texture.isNull()) {
        d.p.setOpacity(0.12); d.p.drawImage(QRectF(1190, 143, 338, 516), d.assets.texture); d.p.setOpacity(1);
    }
    d.p.restore();
    d.text(1217, 165, 291, 40, QStringLiteral("纸间 · 校园一店"), 18, navy, true);
    d.text(1217, 204, 291, 35, state.rangeDate, 14, faded);
    dashed(d, 1217, 244, 291);
    receiptRow(d, 258, QStringLiteral("直接成本"), money(state.business.costCents));
    receiptRow(d, 291, QStringLiteral("贡献利润"), money(state.business.contributionCents));
    receiptRow(d, 326, QStringLiteral("退款金额"), money(state.business.refundCents));
    receiptRow(d, 361, QStringLiteral("待完成金额"), money(state.business.pendingCents));
    dashed(d, 1217, 408, 291);
    d.text(1217, 418, 291, 33, QStringLiteral("履约情况"), 16, navy, true);
    receiptRow(d, 454, QStringLiteral("已完成订单"), QStringLiteral("%1 单").arg(state.business.completed), true);
    receiptRow(d, 488, QStringLiteral("按时完成"), QStringLiteral("%1 单").arg(state.business.onTime), true);
    receiptRow(d, 522, QStringLiteral("超时完成"), QStringLiteral("%1 单").arg(state.business.late), true);
    receiptRow(d, 555, QStringLiteral("当前逾期"), QStringLiteral("%1 单").arg(state.business.overdue), true);
    dashed(d, 1217, 597, 291);
    d.text(1217, 604, 291, 38, QStringLiteral("统计截至 %1").arg(state.clock), 14, faded, false, false, Qt::AlignCenter);
}
// A monotone cubic curve preserves the data values without introducing false peaks.
QPainterPath revenueCurve(const QVector<QPointF>& points) {
    QPainterPath curve;
    if (points.isEmpty()) return curve;
    curve.moveTo(points.front());
    if (points.size() < 2) return curve;
    QVector<double> slopes(points.size() - 1), tangent(points.size());
    for (qsizetype i = 0; i < slopes.size(); ++i) slopes[i] = (points[i + 1].y() - points[i].y()) / (points[i + 1].x() - points[i].x());
    tangent[0] = slopes.front(); tangent[tangent.size() - 1] = slopes.back();
    for (qsizetype i = 1; i < tangent.size() - 1; ++i) tangent[i] = slopes[i - 1] * slopes[i] <= 0 ? 0 : (slopes[i - 1] + slopes[i]) / 2;
    for (qsizetype i = 0; i < slopes.size(); ++i) {
        if (std::abs(slopes[i]) < 0.000001) { tangent[i] = 0; tangent[i + 1] = 0; continue; }
        const double a = tangent[i] / slopes[i], b = tangent[i + 1] / slopes[i];
        const double magnitude = a * a + b * b;
        if (magnitude > 9) {
            const double scale = 3 / std::sqrt(magnitude);
            tangent[i] = scale * a * slopes[i]; tangent[i + 1] = scale * b * slopes[i];
        }
    }
    for (qsizetype i = 0; i < slopes.size(); ++i) {
        const double width = (points[i + 1].x() - points[i].x()) / 3;
        curve.cubicTo(QPointF(points[i].x() + width, points[i].y() + tangent[i] * width),
                      QPointF(points[i + 1].x() - width, points[i + 1].y() - tangent[i + 1] * width), points[i + 1]);
    }
    return curve;
}
void revenueChart(Painter& d, const Snapshot& state) {
    section(d, 40, 328, QStringLiteral("营业趋势"), 180);
    d.text(197, 339, 236, 34, QStringLiteral("每小时营业额"), 14, faded);
    const QRectF plot(96, 400, 997, 181);
    double maximum = 0;
    for (double value : state.hourlyRevenue) maximum = std::max(maximum, value);
    const double ceiling = std::max(120.0, std::ceil(maximum / 40.0) * 40.0);
    d.p.save();
    QPen grid(QColor("#DFE3EB"), 1); grid.setDashPattern({4, 3});
    for (int i = 0; i < 4; ++i) {
        const qreal y = plot.bottom() - plot.height() * i / 3;
        d.p.setPen(grid); d.p.drawLine(QPointF(plot.left(), y), QPointF(plot.right() + 22, y));
        d.text(40, y - 17, 40, 33, QStringLiteral("¥%1").arg(QString::number(ceiling * i / 3, 'f', 0)), 14, faded, false, false, Qt::AlignRight | Qt::AlignVCenter);
    }
    d.p.setPen(QPen(QColor("#BEC5D1"), 1));
    d.p.drawLine(plot.bottomLeft(), plot.topLeft());
    d.p.drawLine(plot.bottomLeft(), QPointF(plot.right() + 22, plot.bottom()));
    const qsizetype count = state.hourlyRevenue.size();
    if (count == 0) {
        d.text(plot.x(), plot.y() + 48, plot.width(), 64, QStringLiteral("暂无营业数据"), 16, faded, false, false, Qt::AlignCenter);
        d.p.restore(); return;
    }
    QVector<QPointF> points;
    qsizetype peak = 0;
    for (qsizetype i = 0; i < count; ++i) {
        const qreal x = count == 1 ? plot.center().x() : plot.left() + plot.width() * i / (count - 1);
        const qreal y = plot.bottom() - plot.height() * std::max(0.0, state.hourlyRevenue[i]) / ceiling;
        points.append(QPointF(x, y));
        if (state.hourlyRevenue[i] > state.hourlyRevenue[peak]) peak = i;
        d.p.setPen(grid); d.p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        d.p.setPen(QPen(QColor("#BEC5D1"), 1)); d.p.drawLine(QPointF(x, plot.bottom()), QPointF(x, plot.bottom() + 7));
        const QString label = (i < state.hourlyLabels.size() ? state.hourlyLabels[i] : QString::number(i)) + QStringLiteral(":00");
        const int stride=std::max(1,int((count+5)/6));
        if(i%stride==0||i==count-1)d.text(x - 42, 587, 84, 30, label, 14, faded, false, false, Qt::AlignCenter);
    }
    d.p.setPen(QPen(navy, 1.8)); d.p.setBrush(Qt::NoBrush); d.p.drawPath(revenueCurve(points));
    for (qsizetype i = 0; i < count; ++i) {
        d.p.setPen(QPen(Qt::white, 1.7)); d.p.setBrush(i == peak ? red : blue); d.p.drawEllipse(points[i], 6.5, 6.5);
        if (i != peak && (count<=6||i%(std::max(1,int((count+5)/6)))==0)) {
            const QString amount = QStringLiteral("¥") + QString::number(state.hourlyRevenue[i], 'f', state.hourlyRevenue[i] == 0 ? 0 : 2);
            const qreal labelX = std::clamp(points[i].x() - 54, plot.left(), plot.right() - 96);
            d.text(labelX, points[i].y() - 32, 108, 27, amount, 14, navy, false, false, Qt::AlignCenter);
        }
    }
    const QPointF peakPoint = points[peak];
    const QRectF tooltip(std::clamp(peakPoint.x() - 79, 97.0, 944.0), std::max(353.0, peakPoint.y() - 63), 158, 35);
    d.panel(tooltip.translated(0, 2), QColor(22, 35, 59, 12), Qt::transparent, 7);
    d.panel(tooltip, Qt::white, QColor("#DFE3EB"), 7);
    QPainterPath notch; notch.moveTo(peakPoint.x() - 7, tooltip.bottom()); notch.lineTo(peakPoint.x(), tooltip.bottom() + 8); notch.lineTo(peakPoint.x() + 7, tooltip.bottom());
    d.p.setPen(QPen(QColor("#DFE3EB"), 1)); d.p.setBrush(Qt::white); d.p.drawPath(notch);
    const QString peakLabel = (peak < state.hourlyLabels.size() ? state.hourlyLabels[peak] : QString::number(peak)) + QStringLiteral(":00");
    d.text(tooltip.x() + 7, tooltip.y(), tooltip.width() - 14, tooltip.height(), peakLabel + QStringLiteral("   ¥") + QString::number(state.hourlyRevenue[peak], 'f', 2), 14, navy, true, false, Qt::AlignCenter);
    d.p.restore();
}
std::array<int, 3> serviceCounts(const Snapshot& state) {
    return state.serviceCounts;
}
QString busyPeriod(const Snapshot& state) {
    if (state.hourlyRevenue.size() < 2 || state.hourlyLabels.size() < 2) return QStringLiteral("暂无营业时段");
    qsizetype end = 1;
    double highest = state.hourlyRevenue[0] + state.hourlyRevenue[1];
    for (qsizetype i = 2; i < state.hourlyRevenue.size(); ++i) {
        const double value = state.hourlyRevenue[i - 1] + state.hourlyRevenue[i];
        if (value > highest) { highest = value; end = i; }
    }
    if (highest <= 0 || end >= state.hourlyLabels.size()) return QStringLiteral("暂无营业时段");
    const qsizetype start = std::max<qsizetype>(0, end - 2);
    return state.hourlyLabels[start] + QStringLiteral(":00 — ") + state.hourlyLabels[end] + QStringLiteral(":00");
}
void serviceIcon(Painter& d, int kind, qreal x, qreal y) {
    if (kind == 1) { d.icon(QStringLiteral("document"), x, y, 27); return; }
    d.p.save(); d.p.setPen(QPen(navy, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); d.p.setBrush(Qt::NoBrush);
    if (kind == 0) {
        QPainterPath bolt; bolt.moveTo(x + 17, y + 1); bolt.lineTo(x + 4, y + 15); bolt.lineTo(x + 13, y + 15); bolt.lineTo(x + 10, y + 26); bolt.lineTo(x + 23, y + 11); bolt.lineTo(x + 15, y + 11); bolt.closeSubpath(); d.p.drawPath(bolt);
    } else {
        for (int row = 0; row < 3; ++row) {
            const qreal yy = y + 5 + row * 8; const qreal knob = x + (row == 1 ? 17 : 9);
            d.p.drawLine(QPointF(x + 3, yy), QPointF(x + 26, yy)); d.p.setBrush(Qt::white); d.p.drawEllipse(QPointF(knob, yy), 3, 3);
        }
    }
    d.p.restore();
}
void breakdown(Painter& d, const Snapshot& state) {
    d.rule(40, 624, 1077); vertical(d, 595, 643, 209);
    section(d, 40, 635, QStringLiteral("打印类型"), 350);
    d.text(451, 641, 105, 35, QStringLiteral("共 %1 页").arg(state.printedPages), 14, navy, false, false, Qt::AlignRight | Qt::AlignVCenter);
    const std::array<int, 2> pages{{state.blackPages, state.colorPages}};
    const QStringList labels{QStringLiteral("黑白"), QStringLiteral("彩色")};
    for (int i = 0; i < 2; ++i) {
        const qreal y = 698 + i * 83;
        d.icon(QStringLiteral("document"), 47, y + 6, 29);
        if (i == 1) {
            d.p.save(); d.p.setPen(Qt::NoPen); d.p.setBrush(QColor("#009CDE")); d.p.drawRect(QRectF(55, y + 24, 6, 5)); d.p.setBrush(red); d.p.drawRect(QRectF(61, y + 24, 6, 5)); d.p.setBrush(QColor("#FFBA00")); d.p.drawRect(QRectF(55, y + 29, 6, 5)); d.p.restore();
        }
        d.text(101, y + 6, 63, 33, labels[i], 16, navy, true);
        const double ratio = fraction(pages[static_cast<std::size_t>(i)], state.printedPages);
        d.progress({164, y + 13, 310, 19}, ratio, i == 0 ? blue : red);
        d.text(482, y - 3, 74, 31, QStringLiteral("%1 页").arg(pages[static_cast<std::size_t>(i)]), 16, navy, true, false, Qt::AlignRight | Qt::AlignVCenter);
        d.text(482, y + 24, 74, 27, QString::number(ratio * 100, 'f', 1) + QStringLiteral("%"), 14, faded, false, false, Qt::AlignRight | Qt::AlignVCenter);
    }
    section(d, 637, 635, QStringLiteral("服务构成"), 420);
    const auto counts = serviceCounts(state);
    const QStringList services{QStringLiteral("快速"), QStringLiteral("普通"), QStringLiteral("灵活")};
    for (int i = 0; i < 3; ++i) {
        const qreal y = 687 + i * 44;
        serviceIcon(d, i, 658, y + 3);
        d.text(710, y - 2, 220, 34, services[i], 16, navy, true);
        d.text(1009, y - 2, 79, 34, QStringLiteral("%1 单").arg(counts[static_cast<std::size_t>(i)]), 16, navy, false, false, Qt::AlignRight | Qt::AlignVCenter);
        d.rule(637, y + 34, 468);
    }
    d.icon(QStringLiteral("clock"), 655, 820, 28);
    d.text(710, 816, 103, 36, QStringLiteral("忙碌时段"), 16, navy, true);
    d.text(811, 816, 291, 36, busyPeriod(state), 16, navy, true);
    section(d, 1175, 690, QStringLiteral("经营观察"), 365);
    d.rule(1175, 738, 375);
    d.p.save(); d.p.setPen(QPen(blue, 4, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 3; ++i) d.p.drawLine(QPointF(1188 + i * 9, 779), QPointF(1188 + i * 9, 764 - i * 6));
    d.p.restore();
    const int promised=state.business.onTime+state.business.late;
    d.text(1238, 750, 315, 40,promised>0 ? QStringLiteral("按时完成率 %1%").arg(QString::number(100.0*state.business.onTime/promised,'f',1)) : QStringLiteral("暂无有取件期限的完成订单"),14,navy,true);
    d.rule(1175, 795, 375);
    d.p.save(); d.p.setPen(QPen(navy, 2)); d.p.setBrush(Qt::NoBrush); d.p.drawEllipse(QRectF(1181, 811, 26, 26));
    QPainterPath pie; pie.moveTo(1194, 824); pie.lineTo(1194, 809); pie.arcTo({1179, 809, 30, 30}, 90, -90); pie.closeSubpath(); d.p.setBrush(blue); d.p.drawPath(pie); d.p.restore();
    d.text(1238,806,315,40,state.business.overdue>0 ? QStringLiteral("%1 单已逾期，请优先处理").arg(state.business.overdue) : QStringLiteral("当前没有逾期订单"),16,state.business.overdue>0?red:navy,true);
}
}

void paintDevices(Painter& d, const Snapshot& state, const Selection& selection) {
    d.headline(58,81,80,QStringLiteral("每台设备，都在"),QStringLiteral("好状态。"));
    d.text(59, 159, 685, 37, QStringLiteral("设备、耗材与任务，随时心中有数。"), 21, faded);
    int connected = 0, printing = 0, idle = 0, alerts = 0, lowDevice = -1;
    for (const Device& device : state.devices) {
        connected += device.online ? 1 : 0;
        printing += device.online && device.busy ? 1 : 0;
        idle += device.online && device.enabled && !device.busy ? 1 : 0;
        bool low = device.paper >= 0 && device.paper < 50;
        for (int toner : device.toner) low = low || (toner >= 0 && toner < 20);
        if (low) { ++alerts; if (lowDevice < 0) lowDevice = device.index; }
    }
    const QStringList labels{QStringLiteral("已连接"), QStringLiteral("打印中"), QStringLiteral("空闲"), QStringLiteral("耗材提醒")};
    const std::array<int, 4> values{{connected, printing, idle, alerts}};
    for (int i = 0; i < 4; ++i)
        d.heroMetric(i,labels[i],number(values[static_cast<std::size_t>(i)]),i==3&&alerts>0?red:navy);
    section(d, 40, 294, QStringLiteral("我的打印设备"), 500);
    d.button({998, 297, 120, 41}, QStringLiteral("添加设备"), QStringLiteral("add-device"));
    const Device* selected = nullptr;
    qsizetype selectedPosition = 0;
    for (qsizetype i = 0; i < state.devices.size(); ++i) if (state.devices[i].index == selection.device) {
        selected = &state.devices[i]; selectedPosition = i; break;
    }
    if (!selected && !state.devices.isEmpty()) selected = &state.devices.front();
    const qsizetype groupStart = (selectedPosition / 2) * 2;
    const qsizetype pageCount=std::max<qsizetype>(1,(state.devices.size()+1)/2);
    d.text(632,297,88,41,QStringLiteral("%1 / %2 页").arg(groupStart/2+1).arg(pageCount),12,slate,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.pageButton({734,297,120,41},QStringLiteral("上一页"),QStringLiteral("device-prev"),{},groupStart>0);
    d.pageButton({866,297,120,41},QStringLiteral("下一页"),QStringLiteral("device-next"),{},groupStart+2<state.devices.size());
    for (qsizetype i = groupStart; i < std::min<qsizetype>(groupStart + 2, state.devices.size()); ++i) {
        const Device& device = state.devices[i];
        deviceCard(d, state, device, i == groupStart ? QRectF(40, 347, 536, 518) : QRectF(588, 347, 536, 518), selected && selected->index == device.index);
    }
    if (state.devices.isEmpty()) {
        d.panel({40, 347, 1081, 518}, QColor(255, 255, 255, 180));
        d.icon(QStringLiteral("printer"), 522, 439, 56);
        d.text(175, 511, 806, 44, QStringLiteral("连接打印设备，开始接收校园订单"), 18, navy, true, false, Qt::AlignCenter);
        d.button({467, 578, 228, 48}, QStringLiteral("添加打印设备"), QStringLiteral("add-device"), {}, true);
    }
    d.panel({40, 877, 1081, 51}, QColor("#FCF6ED"), QColor("#EEE8DE"), 6);
    d.icon(QStringLiteral("printer"), 65, 887, 31);
    QString reminder = QStringLiteral("耗材状态正常，设备可持续接单。");
    if (lowDevice >= 0) {
        for (const Device& device : state.devices) if (device.index == lowDevice) {
            const int toner = *std::min_element(device.toner.begin(), device.toner.end(), [](int a, int b) { return (a < 0 ? 101 : a) < (b < 0 ? 101 : b); });
            reminder = device.name + (toner >= 0 && toner < 20 ? QStringLiteral(" 的墨粉剩余 %1%，建议提前备货。").arg(toner) : QStringLiteral(" 的纸张余量较低，建议提前补充。"));
            break;
        }
    } else if (selected && selected->paper < 0) reminder = QStringLiteral("后台提供共享纸张库存，尚未提供每台设备的耗材遥测。");
    d.text(119, 883, 865, 39, reminder, 14, navy, true);
    d.text(1015, 882, 74, 39, QStringLiteral("查看耗材"), 14, faded); smallArrow(d, 1093, 896, faded);
    d.hit({995, 877, 126, 51}, QStringLiteral("restock"), lowDevice < 0 && selected ? number(selected->index) : number(lowDevice), QStringLiteral("查看耗材"));
    deviceDetails(d, state, selected);
}

StatsMetricLayout statsMetricLayout(const Snapshot& state) {
    const QStringList values{money(state.revenueCents),number(state.todayOrders),number(state.printedPages),QString::number(state.averageMinutes,'f',1)};
    const QStringList labels{state.rangeLabel+QStringLiteral("营业额"),QStringLiteral("订单总数"),QStringLiteral("打印页数"),QStringLiteral("平均完成")};
    const QFontMetricsF labelMetrics(merchantFont(14));
    const qreal unitWidth=QFontMetricsF(merchantFont(14,true)).horizontalAdvance(QStringLiteral("分钟"))+8;
    constexpr qreal left=59,right=805,gap=40;
    const qreal available=right-left-3*gap;
    StatsMetricLayout layout;
    std::array<qreal,4> needed{};
    qreal total=0;
    do {
        total=0;
        const QFontMetricsF metrics(merchantFont(layout.valueSize,true));
        for (int i=0;i<4;++i) {
            needed[static_cast<std::size_t>(i)]=std::max(labelMetrics.horizontalAdvance(labels[i]),metrics.horizontalAdvance(values[i])+(i==3?unitWidth:0));
            total+=needed[static_cast<std::size_t>(i)];
        }
        if (total<=available || layout.valueSize==1) break;
        --layout.valueSize;
    } while (true);
    // Each divider follows the measured content width, with a shared gutter.
    // Only pathological simultaneous values require a uniform type reduction.
    const qreal spare=std::max<qreal>(0,(available-total)/4);
    qreal x=left;
    for (int i=0;i<4;++i) {
        const qreal width=needed[static_cast<std::size_t>(i)]+spare;
        layout.columns[static_cast<std::size_t>(i)]={x,206,width,72};
        if (i<3) layout.dividers[static_cast<std::size_t>(i)]=x+width+gap/2;
        x+=width+gap;
    }
    return layout;
}

void paintStats(Painter& d, const Snapshot& state, const Selection& selection) {
    d.headline(58,81,80,QStringLiteral("今天的生意，"),QStringLiteral("一目了然。"));
    d.text(59,159,650,37,QStringLiteral("把每一份打印，记成清楚的账。"),21,faded);
    const auto layout=statsMetricLayout(state);
    const QStringList labels{state.rangeLabel+QStringLiteral("营业额"),QStringLiteral("订单总数"),QStringLiteral("打印页数"),QStringLiteral("平均完成")};
    const QStringList values{money(state.revenueCents),number(state.todayOrders),number(state.printedPages),QString::number(state.averageMinutes,'f',1)};
    for (int i=0;i<4;++i) {
        const QRectF column=layout.columns[static_cast<std::size_t>(i)];
        d.text(column.x(),206,column.width(),27,labels[i],14,faded);
        d.text(column.x(),235,column.width(),43,values[i],layout.valueSize,navy,true);
        if (i==3) {
            const QFontMetricsF valueMetrics(merchantFont(layout.valueSize,true)),unitMetrics(merchantFont(14,true));
            const qreal offset=(valueMetrics.ascent()-valueMetrics.descent()-unitMetrics.ascent()+unitMetrics.descent())/2;
            d.text(column.x()+valueMetrics.horizontalAdvance(values[i])+8,235+offset,unitMetrics.horizontalAdvance(QStringLiteral("分钟"))+2,43,QStringLiteral("分钟"),14,navy,true);
        }
        if (i<3) vertical(d,layout.dividers[static_cast<std::size_t>(i)],211,56);
    }
    const QStringList ranges{QStringLiteral("今天"), QStringLiteral("本周"), QStringLiteral("本月"), QStringLiteral("自定义")};
    d.panel({825,200,292,50},Qt::white,QColor("#BBC3CF"),9);
    for (int i = 0; i < 4; ++i) {
        const QRectF rect(829+i*71,204,71,42);
        const bool active = selection.range == ranges[i];
        if (active) {d.p.save();d.p.setPen(QPen(red,1.5));d.p.setBrush(Qt::white);d.p.drawRoundedRect(rect.adjusted(2,1,-2,-1),7,7);d.p.restore();}
        else if (i>0 && selection.range!=ranges[i-1]) vertical(d,rect.x(),214,22);
        d.text(rect.x(), rect.y(), rect.width(), rect.height(), ranges[i], 14, active ? red : navy, false, false, Qt::AlignCenter);
        d.hit(rect, QStringLiteral("range"), ranges[i], ranges[i]);
    }
    d.icon(QStringLiteral("calendar"), 967, 260, 18, faded);
    d.text(995, 252, 123, 35, state.rangeDate, 14, faded, false, false, Qt::AlignRight | Qt::AlignVCenter);
    revenueChart(d, state); receipt(d, state); breakdown(d, state);
    d.icon(QStringLiteral("calendar"), 45, 879, 38);
    d.text(101, 873, 311, 34, selection.range == QStringLiteral("今天") ? QStringLiteral("今日营业数据") : selection.range + QStringLiteral("营业数据"), 21, navy, true);
    d.text(101, 906, 311, 29, state.rangeDate, 12, faded);
    vertical(d, 435, 883, 37);
    d.icon(QStringLiteral("clock"), 478, 883, 36);
    d.text(536, 874, 478, 34, QStringLiteral("统计截至 %1").arg(state.clock), 21, navy, true);
    d.text(536, 906, 478, 29, QStringLiteral("订单、页数与营业额同步汇总"), 12, faded);
    d.button({1077, 875, 193, 54}, QStringLiteral("查看订单明细"), QStringLiteral("order-details"));
    d.button({1286, 875, 262, 54}, QStringLiteral("导出营业报表    →"), QStringLiteral("export-report"), {}, true);
}
}
