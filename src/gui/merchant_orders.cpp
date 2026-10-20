#include "merchant_painter.hpp"
#include "view_text.hpp"
#include <algorithm>
#include <cmath>
#include <QFontMetricsF>

namespace cps::gui::merchant {
namespace {
const QColor secondary = slate;
const QColor selectedFill("#FFF2EC");
const QColor printingFill("#EEF5FF");
const QColor warning("#B8382A");

const Order* selectedOrder(const Snapshot& state, const Selection& selection)
{
    return selection.order >= 0 && selection.order < state.orders.size()
        ? &state.orders[selection.order] : nullptr;
}

const Device* selectedDevice(const Snapshot& state, const Selection& selection)
{
    return selection.device >= 0 && selection.device < state.devices.size()
        ? &state.devices[selection.device] : nullptr;
}

const Device* orderDevice(const Snapshot& state, const Order& order)
{
    return order.printer >= 0 && order.printer < state.devices.size() ? &state.devices[order.printer] : nullptr;
}

QString pickup(const Order& order)
{
    return order.pickupTime.isEmpty() ? QStringLiteral("未指定") : order.pickupTime;
}

QString deviceStatus(const Device& device)
{
    if (!device.online) return QStringLiteral("停止接单");
    return device.busy ? QStringLiteral("打印中") : QStringLiteral("空闲");
}

// What a live waiting order is waiting for, in the backend's own forecast terms.
QString waitingText(const Snapshot& state, const Order& order)
{
    if (!order.blocked.isEmpty()) return text::blocked(order.blocked);
    if (order.startInMs < 0) return QStringLiteral("等待后台推算");
    return state.paused ? text::afterResume(order.startInMs) : text::startsIn(order.startInMs);
}

QString closedText(const Order& order)
{
    return order.state == OrderState::Failed ? QStringLiteral("打印失败") : QStringLiteral("订单已取消");
}

void verticalRule(Painter& d, qreal x, qreal y, qreal height)
{
    d.p.save();
    d.p.setPen(QPen(ruleColor, 1.4));
    d.p.drawLine(QPointF(x,y), QPointF(x,y+height));
    d.p.restore();
}

void selectedEdge(Painter& d, const QRectF& rect)
{
    d.p.save();
    d.p.setPen(Qt::NoPen);
    d.p.setBrush(red);
    d.p.drawRoundedRect(QRectF(rect.x(),rect.y(),4,rect.height()),2,2);
    d.p.restore();
}

void serviceBadge(Painter& d, const QRectF& rect, cps::ServiceTier service)
{
    d.badge(rect,serviceLabel(service),service == cps::ServiceTier::Express);
}

void colorDots(Painter& d, qreal x, qreal y, qreal radius)
{
    d.p.save(); d.p.setPen(Qt::NoPen);
    const QColor colors[]={QColor("#16A3DE"),red,QColor("#F2C000")};
    for (int c=0;c<3;++c) { d.p.setBrush(colors[c]); d.p.drawEllipse(QPointF(x+c*radius*2.1,y),radius,radius); }
    d.p.restore();
}

void refreshButton(Painter& d, const QRectF& rect)
{
    d.panel(rect,Qt::white,navy,6);
    d.icon(QStringLiteral("refresh"),rect.x()+12,rect.y()+10,19);
    d.text(rect.x()+39,rect.y()+3,rect.width()-43,rect.height()-6,QStringLiteral("刷新"),14,navy,true);
    d.hit(rect,QStringLiteral("refresh"),{},QStringLiteral("刷新订单与设备状态"));
}

void browseArrow(Painter& d, const QRectF& rect, bool next, const QString& action,
                 int target, bool enabled, const QString& label)
{
    d.panel(rect,Qt::white,enabled ? QColor("#B8C4D9") : ruleColor,3);
    d.text(rect.x(),rect.y(),rect.width(),rect.height(),next ? QStringLiteral("›") : QStringLiteral("‹"),
           16,enabled ? navy : QColor("#C0C5CE"),true,false,Qt::AlignCenter);
    if (enabled) d.hit(rect,action,QString::number(target),label);
}

void hero(Painter& d, const Snapshot& state, bool queue)
{
    if (queue) {
        d.headline(58,81,80,QStringLiteral("每一单，都有"),QStringLiteral("好次序。"));
        d.text(59,159,690,37,state.showcase?QStringLiteral("按设备查看进度，拖动工单调整顺序。"):QStringLiteral("按当前策略自动派发，预计时间由后台推算。"),21,secondary);
        if (!state.showcase) {
            d.heroMetric(0,QStringLiteral("待打印"),QString::number(state.waiting),red);
            d.heroMetric(1,QStringLiteral("打印中"),QString::number(state.printing));
            d.heroMetric(2,QStringLiteral("受阻"),QString::number(state.blocked),state.blocked>0?red:navy);
            if (state.clearMinutes<0) d.heroMetric(3,QStringLiteral("预计清空"),QStringLiteral("—"));
            else d.heroMetric(3,QStringLiteral("预计清空"),QString::number(state.clearMinutes),navy,QStringLiteral("分钟"));
            return;
        }
        int assigned=0, unassigned=0;
        for (const auto& order : state.orders) {
            if (order.state != OrderState::Waiting) continue;
            order.printer >= 0 ? ++assigned : ++unassigned;
        }
        d.heroMetric(0,QStringLiteral("待打印"),QString::number(state.waiting),red);
        d.heroMetric(1,QStringLiteral("打印中"),QString::number(state.printing));
        d.heroMetric(2,QStringLiteral("已分配"),QString::number(assigned));
        d.heroMetric(3,QStringLiteral("未分配"),QString::number(unassigned));
    } else {
        d.headline(58,81,80,QStringLiteral("每一份，都值得"),QStringLiteral("印好。"));
        d.text(59,159,630,37,QStringLiteral("订单、队列与设备，一眼掌握。"),21,secondary);
        d.heroMetric(0,state.rangeLabel+QStringLiteral("订单"),QString::number(state.todayOrders));
        d.heroMetric(1,QStringLiteral("待打印"),QString::number(state.waiting),red);
        d.heroMetric(2,QStringLiteral("打印中"),QString::number(state.printing));
        // The shared backend has no pickup step: its printed orders are simply completed.
        if (state.showcase) d.heroMetric(3,QStringLiteral("待取件"),QString::number(state.ready));
        else d.heroMetric(3,QStringLiteral("已完成"),QString::number(state.completed));
        d.text(971,210,134,29,state.rangeLabel+QStringLiteral("营收"),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
        d.text(937,238,168,40,money(state.revenueCents),31,navy,true,false,Qt::AlignRight|Qt::AlignVCenter);
    }
}

void documentPreview(Painter& d, const Order& order, const QRectF& rect)
{
    if (order.title.contains(QStringLiteral("操作系统"))) {
        d.cover(rect);
    } else if (order.title.contains(QStringLiteral("海报")) && !d.assets.queue.isNull()) {
        d.p.drawImage(rect,d.assets.queue,QRectF(698,653,55,74));
    } else {
        d.p.save();
        d.p.translate(rect.center());
        d.p.rotate(-5);
        const QRectF sheet(-rect.width()/2,-rect.height()/2,rect.width(),rect.height());
        d.panel(sheet,Qt::white,ruleColor,1);
        d.text(sheet.x()+rect.width()*.11,sheet.y()+rect.height()*.12,rect.width()*.78,rect.height()*.26,
               order.title,rect.width() > 100 ? 24 : 6,navy,true,true);
        d.p.setPen(QPen(QColor("#CBD2DE"),rect.width() > 100 ? 1.5 : .6));
        for (int i=0;i<8;++i) {
            const qreal y=sheet.y()+rect.height()*(.48+.045*i);
            d.p.drawLine(QPointF(sheet.x()+rect.width()*.12,y),QPointF(sheet.right()-rect.width()*.12,y));
        }
        d.p.restore();
    }
}

void sidebarHeading(Painter& d, const Order* order, bool queue)
{
    d.text(1175,79,375,52,queue ? QStringLiteral("队列详情") : QStringLiteral("订单详情"),18,navy,true);
    if (!order) {
        d.emptyState(QRectF(1175,200,378,260),queue ? QStringLiteral("printer") : QStringLiteral("document"),
                     queue ? QStringLiteral("选择工单查看派发与进度") : QStringLiteral("选择订单查看详情"));
        return;
    }
    if (queue) {
        d.text(1175,134,255,30,order->id,14,secondary);
        d.statusBadge(QRectF(1450,134,103,30),order->state);
    }
}

// Live orders are matched to printers by the backend; the owner only sees the result.
void liveDispatchRows(Painter& d, const Snapshot& state, const Order& order)
{
    d.rule(1175,887,378);
    d.text(1175,893,105,46,QStringLiteral("打印设备"),14,navy,true);
    const Device* device=orderDevice(state,order);
    QString name=device ? device->name : QStringLiteral("后台自动分配");
    QString detail;
    QColor tone=secondary;
    if (order.state==OrderState::Waiting) {
        name=QStringLiteral("后台自动分配");
        detail=waitingText(state,order);
        if (!order.blocked.isEmpty()) tone=warning;
    } else if (isActive(order.state)) {
        detail=order.state==OrderState::Cancelling ? QStringLiteral("正在取消") : QStringLiteral("正在打印 %1 / %2 页").arg(order.printed).arg(order.pages*order.copies);
    } else if (isPrinted(order.state)) {
        detail=order.finishedMs>0 ? QDateTime::fromMSecsSinceEpoch(order.finishedMs).toString(QStringLiteral("M月d日 HH:mm 完成")) : QStringLiteral("打印已完成");
    } else {
        detail=closedText(order);
        if (!device) name=QStringLiteral("—");
    }
    d.text(1286,891,267,26,name,14,navy,true,false,Qt::AlignRight|Qt::AlignVCenter);
    d.text(1286,916,267,22,detail,12,tone,false,false,Qt::AlignRight|Qt::AlignVCenter);
}

void workbenchSidebar(Painter& d, const Snapshot& state, const Selection& selection)
{
    const Order* order=selectedOrder(state,selection);
    sidebarHeading(d,order,false);
    if (!order) return;
    const QImage cover=d.assets.orderCover(order->coverPath);
    if(!cover.isNull()) {
        const QRectF area(1175,143,378,370);
        QSizeF size=cover.size();size.scale(area.size(),Qt::KeepAspectRatio);
        const QRectF paper(area.center()-QPointF(size.width()/2,size.height()/2),size);
        d.panel(paper.translated(3,4),QColor(20,33,62,12),Qt::transparent,3);
        d.p.drawImage(paper,cover);
    } else if (state.showcase) documentPreview(d,*order,QRectF(1240,143,248,370));
    else {
        d.panel({1240,143,248,370},QColor("#FBFBFA"),ruleColor,4);
        d.icon(QStringLiteral("document"),1335,238,58,QColor("#B4BDCC"));
        d.text(1248,317,232,36,QStringLiteral("暂无文件封面"),16,navy,true,false,Qt::AlignCenter);
        d.text(1248,350,232,33,QStringLiteral("可在“查看文件”中补充"),14,secondary,false,false,Qt::AlignCenter);
    }
    d.panel(QRectF(1303,525,124,35),Qt::white,ruleColor,18);
    d.icon(QStringLiteral("search"),1325,531,20);
    d.text(1350,525,67,35,QStringLiteral("查看文件"),14,navy,true);
    d.hit(QRectF(1303,525,124,35),QStringLiteral("view-file"),QString::number(selection.order),QStringLiteral("查看文件"));
    d.text(1175,570,259,44,order->title,16,navy,true);
    d.statusBadge(QRectF(1450,577,103,30),order->state);
    d.rule(1175,620,378);
    d.text(1175,633,350,29,QStringLiteral("打印参数"),16,navy,true);
    d.detailRow(672,QStringLiteral("纸张"),order->a3 ? QStringLiteral("A3") : QStringLiteral("A4"));
    d.detailRow(707,QStringLiteral("页数 / 份数"),QStringLiteral("%1 页 / %2 份").arg(order->pages).arg(order->copies));
    d.detailRow(742,QStringLiteral("色彩 / 单双面"),QStringLiteral("%1 / %2").arg(order->color ? QStringLiteral("彩色") : QStringLiteral("黑白"),order->duplex ? QStringLiteral("双面") : QStringLiteral("单面")));
    d.detailRow(777,QStringLiteral("打印服务"),serviceLabel(order->service));
    d.detailRow(812,QStringLiteral("订单金额"),money(order->amountCents));
    d.detailRow(847,QStringLiteral("取件时间"),pickup(*order)+QStringLiteral(" 前"));
    if (!state.showcase) { liveDispatchRows(d,state,*order); return; }
    d.rule(1175,887,378);
    d.text(1175,897,105,38,QStringLiteral("分配打印机"),14,navy,true);
    const Device* device=selectedDevice(state,selection);
    const QString name=device ? device->name : QStringLiteral("选择设备");
    d.panel(QRectF(1274,897,162,38),Qt::white,QColor("#B9C3D7"),5);
    d.text(1286,900,116,32,name,14,navy,true);
    d.icon(QStringLiteral("chevron-down"),1410,908,18,secondary);
    const int next=state.devices.isEmpty() ? -1 : (selection.device+1)%state.devices.size();
    d.hit(QRectF(1274,897,162,38),QStringLiteral("select-device"),QString::number(next),QStringLiteral("选择打印设备"));
    d.dot(1462,909,device && device->online ? QStringLiteral("设备在线") : QStringLiteral("设备离线"),device && device->online ? green : slate);
    d.text(1450,922,112,18,device && device->paper > 0 ? QStringLiteral("纸张充足，可开始打印") : QStringLiteral("请检查设备纸张"),11,secondary);
}

void queueSidebar(Painter& d, const Snapshot& state, const Selection& selection)
{
    const Order* order=selectedOrder(state,selection);
    sidebarHeading(d,order,true);
    if (!order) return;
    d.text(1175,176,378,48,order->title,16,navy,true);
    const Device* device=orderDevice(state,*order);
    int position=0;
    for (int i=0;i<state.orders.size();++i) {
        if (state.orders[i].printer==order->printer && state.orders[i].state==OrderState::Waiting) ++position;
        if (i==selection.order) break;
    }
    const bool waiting=order->state==OrderState::Waiting;
    const bool printing=isActive(order->state);
    const bool completed=isPrinted(order->state);
    const bool stopped=isClosed(order->state);
    const bool blocked=waiting && !order->blocked.isEmpty();
    const int total=std::max(1,order->pages*order->copies);
    const double progress=std::clamp(double(order->printed)/total,0.,1.);
    const QString location=printing ? (order->state==OrderState::Cancelling ? QStringLiteral("正在取消") : QStringLiteral("正在打印"))
        : completed ? QStringLiteral("已完成打印") : stopped ? stateLabel(order->state)
        : device ? QStringLiteral("设备队列第 %1 单").arg(std::max(1,position))
        : state.showcase ? QStringLiteral("等待自动分配") : QStringLiteral("等待队列第 %1 位").arg(std::max(1,position));

    d.panel({1175,245,378,120},Qt::white,ruleColor,6);
    d.icon(QStringLiteral("printer"),1195,269,43,device ? navy : secondary);
    d.text(1256,258,277,36,device ? device->name : QStringLiteral("等待分配设备"),16,navy,true);
    d.text(1256,301,277,29,location,14,secondary);
    d.text(1195,334,338,22,device ? (device->online ? QStringLiteral("设备在线") : QStringLiteral("设备已停止接单")) :
        blocked ? text::blocked(order->blocked) :
        state.paused && waiting ? QStringLiteral("派发已暂停，恢复后自动匹配") : QStringLiteral("后台按调度策略匹配兼容设备"),12,blocked ? warning : secondary);

    d.text(1175,391,378,32,QStringLiteral("打印进度"),16,navy,true);
    d.text(1175,430,264,42,QStringLiteral("%1 / %2 页").arg(order->printed).arg(total),26,navy,true);
    d.text(1462,432,91,40,QStringLiteral("%1%").arg(qRound(progress*100)),18,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.progress({1175,482,378,10},progress,navy);
    d.text(1175,504,378,28,printing ? QStringLiteral("设备正在处理这份工单") : completed ? QStringLiteral("打印完成，等待学生取件") :
        stopped ? closedText(*order) : !state.showcase ? waitingText(state,*order) :
        state.paused ? QStringLiteral("当前暂停派发") : QStringLiteral("尚未开始打印"),14,blocked ? warning : secondary);
    d.rule(1175,551,378);

    // Queue details explain the operational stages, never repeat the file preview.
    const QStringList stages{QStringLiteral("提交入队"),QStringLiteral("设备派发"),QStringLiteral("打印完成")};
    const QStringList descriptions{
        QStringLiteral("已接收工单，按当前策略排队"),
        device ? device->name : state.paused && waiting ? QStringLiteral("等待恢复派发") : stopped ? QStringLiteral("工单已停止") : QStringLiteral("等待兼容设备空闲"),
        completed ? QStringLiteral("本次打印已完成") : stopped ? closedText(*order) : order->estimatedMinutes>=0 ? QStringLiteral("预计 %1 分钟完成").arg(order->estimatedMinutes) : QStringLiteral("完成时间待后台确认")};
    for (int step=0;step<3;++step) {
        const qreal y=574+step*62;
        const bool reached=step==0 || (step==1 && device) || (step==2 && completed);
        d.p.save();
        d.p.setPen(QPen(ruleColor,1.4));
        if (step<2) d.p.drawLine(QPointF(1183,y+18),QPointF(1183,y+62));
        d.p.setPen(Qt::NoPen); d.p.setBrush(reached ? green : ruleColor);
        d.p.drawEllipse(QPointF(1183,y+12),4,4); d.p.restore();
        d.text(1205,y-4,348,28,stages[step],14,navy,true);
        d.text(1205,y+22,348,27,descriptions[step],12,secondary);
    }
    d.rule(1175,754,378);
    d.detailRow(766,QStringLiteral("打印规格"),QStringLiteral("%1 / %2 / %3").arg(order->a3 ? QStringLiteral("A3") : QStringLiteral("A4"),order->color ? QStringLiteral("彩色") : QStringLiteral("黑白"),order->duplex ? QStringLiteral("双面") : QStringLiteral("单面")));
    d.detailRow(801,QStringLiteral("页数 / 份数"),QStringLiteral("%1 页 / %2 份").arg(order->pages).arg(order->copies));
    d.detailRow(836,QStringLiteral("打印服务"),serviceLabel(order->service));
    d.detailRow(871,QStringLiteral("取件时间"),pickup(*order));
}

// Sample devices keep the concept's two wide cards with per-device queues.
void showcaseDeviceStrip(Painter& d, const Snapshot& state, const Selection& selection)
{
    const int base=std::max(0,selection.device/2*2);
    for (int lane=0;lane<2 && base+lane<state.devices.size();++lane) {
        const int i=base+lane;
        const Device& device=state.devices[i];
        const qreal x=lane==0 ? 39 : 587;
        d.panel(QRectF(x,762,536,90),Qt::white,ruleColor,4);
        d.hit(QRectF(x,762,536,90),QStringLiteral("select-device"),QString::number(i),device.name);
        d.icon(QStringLiteral("printer"),x+24,780,46);
        if (device.color) colorDots(d,x+37,821,4.4);
        const qreal nameWidth=std::min(QFontMetricsF(merchantFont(14,true)).horizontalAdvance(device.name)+2,qreal(191));
        d.text(x+94,775,nameWidth,26,device.name,14,navy,true);
        d.dot(x+94+nameWidth+18,789,device.online ? (device.busy ? QStringLiteral("打印中") : QStringLiteral("空闲")) : QStringLiteral("离线"),device.online ? green : slate);
        const double ratio=device.total>0 ? double(device.printed)/device.total : 0;
        d.progress(QRectF(x+94,807,300,8),ratio,QColor("#355784"));
        d.text(x+94,821,310,24,device.busy ? QStringLiteral("%1 · %2 / %3 页").arg(device.jobId).arg(device.printed).arg(device.total) : QStringLiteral("可接收新任务"),12,secondary);
        verticalRule(d,x+426,782,51);
        d.text(x+455,781,69,25,QStringLiteral("队列"),14,secondary);
        d.text(x+455,807,69,28,QStringLiteral("%1 单").arg(device.queued),14,navy,true);
    }
}

// Live devices: the three most active of the six slots, busy ones first.
void liveDeviceStrip(Painter& d, const Snapshot& state)
{
    QVector<int> order;
    for (int i=0;i<state.devices.size();++i) order.append(i);
    std::stable_sort(order.begin(),order.end(),[&state](int a,int b){
        const auto rank=[](const Device& device){return device.busy ? 0 : device.online ? 1 : 2;};
        return rank(state.devices[a])<rank(state.devices[b]);
    });
    for (int lane=0;lane<3 && lane<order.size();++lane) {
        const Device& device=state.devices[order[lane]];
        const QRectF card(39+lane*363,762,353,90);
        d.panel(card,Qt::white,ruleColor,4);
        d.hit(card,QStringLiteral("select-device"),QString::number(device.index),device.name);
        d.icon(QStringLiteral("printer"),card.x()+18,782,38,device.online ? navy : secondary);
        if (device.color) colorDots(d,card.x()+28,816,3.6);
        const qreal nameWidth=std::min(QFontMetricsF(merchantFont(14,true)).horizontalAdvance(device.name)+2,qreal(150));
        d.text(card.x()+72,773,nameWidth,26,device.name,14,navy,true);
        d.dot(card.x()+72+nameWidth+16,786,deviceStatus(device),device.online ? green : slate);
        if (device.busy) {
            const double ratio=device.total>0 ? double(device.printed)/device.total : 0;
            d.progress(QRectF(card.x()+72,806,card.width()-92,7),ratio,QColor("#355784"));
            d.text(card.x()+72,818,card.width()-92,24,QStringLiteral("%1 · %2 / %3 页").arg(device.jobId).arg(device.printed).arg(device.total),12,secondary);
        } else {
            d.text(card.x()+72,806,card.width()-92,22,capability(device),12,secondary);
            d.text(card.x()+72,826,card.width()-92,20,QStringLiteral("今日 %1 页").arg(device.todayPages),12,secondary);
        }
    }
}

void deviceStrip(Painter& d, const Snapshot& state, const Selection& selection)
{
    d.text(39,726,160,30,state.showcase ? QStringLiteral("设备与队列") : QStringLiteral("设备状态"),18,navy,true);
    if (!state.showcase) {
        const auto busy=std::count_if(state.devices.cbegin(),state.devices.cend(),[](const Device& device){return device.busy;});
        d.text(150,729,300,27,QStringLiteral("%1 台 · %2 台打印中").arg(state.devices.size()).arg(busy),12,secondary);
    }
    d.text(958,729,158,27,QStringLiteral("查看全部设备  ›"),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.hit(QRectF(957,727,163,28),QStringLiteral("page"),QStringLiteral("devices"),QStringLiteral("查看全部设备"));
    if (state.devices.isEmpty()) d.text(39,773,1081,63,QStringLiteral("尚未发现打印设备"),16,secondary,false,false,Qt::AlignCenter);
    else if (state.showcase) showcaseDeviceStrip(d,state,selection);
    else liveDeviceStrip(d,state);
}

void workbenchFooter(Painter& d, const Snapshot& state, const Selection& selection)
{
    const Order* order=selectedOrder(state,selection);
    const Device* device=selectedDevice(state,selection);
    d.icon(QStringLiteral("stack"),44,881,38);
    d.text(101,876,306,30,order ? QStringLiteral("已选 1 个订单") : QStringLiteral("请选择订单"),21,navy,true);
    d.text(102,907,312,26,order ? specs(*order) : QStringLiteral("从工作台选择需要打印的文件"),12,secondary);
    verticalRule(d,435,882,38);
    d.icon(QStringLiteral("clock"),477,883,39);
    const int minutes=order ? state.showcase?std::max(1,int(std::ceil(double(order->pages*order->copies)/5.0))):order->estimatedMinutes : -1;
    QString forecast=minutes<0?QStringLiteral("暂无法估算完成时间"):QStringLiteral("预计 %1 分钟完成").arg(minutes);
    QString detail;
    if (state.showcase) detail=device ? QStringLiteral("发送至 %1").arg(device->name) : QStringLiteral("请先分配打印设备");
    else if (order && order->state==OrderState::Waiting) detail=state.paused ? QStringLiteral("派发已暂停，恢复后自动匹配设备") : QStringLiteral("由后台按当前策略自动匹配设备");
    else if (order && orderDevice(state,*order)) detail=QStringLiteral("打印设备：%1").arg(orderDevice(state,*order)->name);
    if (order && isPrinted(order->state)) forecast=QStringLiteral("打印已完成");
    else if (order && isClosed(order->state)) forecast=closedText(*order);
    else if (order && order->state==OrderState::Cancelling) forecast=QStringLiteral("正在取消");
    else if (order && !state.showcase && order->state==OrderState::Waiting && !order->blocked.isEmpty()) forecast=text::blocked(order->blocked);
    d.text(534,876,500,30,forecast,21,navy,true);
    d.text(535,907,495,26,detail,12,secondary);
}

void orderTabs(Painter& d, const Snapshot& state, const Selection& selection)
{
    const QFontMetricsF labelMetrics(merchantFont(14)),countMetrics(merchantFont(12));
    qreal x=267;
    for (const OrderFilter filter : orderFilters(state.showcase)) {
        const QString label=filterLabel(filter);
        const int count=filterCount(state,filter);
        const qreal labelWidth=labelMetrics.horizontalAdvance(label)+2;
        // Six sample tabs must end well before the search box at x=796.
        const qreal width=labelWidth+countMetrics.horizontalAdvance(QString::number(count))+26;
        const QRectF rect(x,311,width,48);
        d.text(rect.x()+10,315,labelWidth,31,label,14,navy);
        d.text(rect.x()+15+labelWidth,315,width-labelWidth-15,31,QString::number(count),12,filter==OrderFilter::Waiting && count>0 ? red : secondary);
        d.hit(rect,QStringLiteral("filter"),QString::number(static_cast<int>(filter)),QStringLiteral("查看%1订单").arg(label));
        // A search covers every order, so no single tab is current while it is active.
        if (selection.filter==filter && selection.search.isEmpty()) {
            d.p.save(); d.p.setPen(QPen(red,4)); d.p.drawLine(QPointF(rect.x(),358),QPointF(rect.right(),358)); d.p.restore();
        }
        x+=width;
    }
}

void queuedCard(Painter& d, const Snapshot& state, const Selection& selection, int orderIndex, const QRectF& rect, bool thumbnail)
{
    const Order& order=state.orders[orderIndex];
    const bool selected=selection.order==orderIndex;
    d.panel(rect,selected ? selectedFill : Qt::white,ruleColor,5);
    if (selected) selectedEdge(d,rect);
    d.hit(rect,QStringLiteral("select-order"),QString::number(orderIndex),QStringLiteral("选择订单 %1 %2").arg(order.id,order.title));
    d.icon(QStringLiteral("grip"),rect.x()+14,rect.y()+rect.height()/2-12,23,selected ? secondary : QColor("#96A7C8"));
    d.hit(QRectF(rect.x()+9,rect.y()+10,31,rect.height()-20),QStringLiteral("reorder"),QString::number(orderIndex),QStringLiteral("调整 %1 队列顺序").arg(order.id));
    const qreal titleX=rect.x()+(thumbnail ? 137 : 99);
    if (thumbnail) documentPreview(d,order,QRectF(rect.x()+58,rect.y()+10,55,74));
    else d.icon(QStringLiteral("document"),rect.x()+51,rect.y()+20,32);
    d.text(titleX,rect.y()+9,rect.width()-(titleX-rect.x())-13,26,order.title,14,navy,true);
    d.text(titleX,rect.y()+34,74,23,order.id,12,secondary);
    if (thumbnail) {
        d.text(titleX,rect.y()+58,rect.width()-(titleX-rect.x())-12,23,specs(order),14,secondary);
        serviceBadge(d,QRectF(rect.right()-169,rect.y()+35,58,28),order.service);
        d.text(rect.right()-108,rect.y()+35,99,28,pickup(order)+QStringLiteral(" 前取件"),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    } else {
        d.text(titleX+82,rect.y()+34,186,23,specs(order),14,secondary);
        serviceBadge(d,QRectF(rect.right()-182,rect.y()+24,59,28),order.service);
        d.text(rect.right()-116,rect.y()+24,105,28,pickup(order)+QStringLiteral(" 前取件"),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    }
}

void queueLane(Painter& d, const Snapshot& state, const Selection& selection, int deviceIndex, const QRectF& rect)
{
    d.panel(rect,Qt::white,ruleColor,5);
    if (deviceIndex<0 || deviceIndex>=state.devices.size()) {
        d.text(rect.x()+20,rect.y()+20,rect.width()-40,40,QStringLiteral("暂无设备"),16,secondary);
        return;
    }
    const Device& device=state.devices[deviceIndex];
    d.hit(QRectF(rect.x(),rect.y(),rect.width(),63),QStringLiteral("select-device"),QString::number(deviceIndex),device.name);
    d.icon(QStringLiteral("printer"),rect.x()+21,rect.y()+13,42);
    if (device.color) colorDots(d,rect.x()+32,rect.y()+50,4);
    const qreal nameWidth=std::min(QFontMetricsF(merchantFont(16,true)).horizontalAdvance(device.name)+2,rect.width()-290);
    d.text(rect.x()+82,rect.y()+16,nameWidth,31,device.name,16,navy,true);
    d.dot(rect.x()+82+nameWidth+18,rect.y()+32,device.online ? (device.busy ? QStringLiteral("打印中") : QStringLiteral("空闲")) : QStringLiteral("离线"),device.online ? green : slate);
    d.text(rect.right()-113,rect.y()+17,88,30,QStringLiteral("%1 单等待").arg(device.queued),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.rule(rect.x(),rect.y()+63,rect.width());
    QVector<int> waiting;
    int active=-1;
    for (int i=0;i<state.orders.size();++i) {
        const Order& order=state.orders[i];
        if (order.printer!=deviceIndex) continue;
        if (order.state==OrderState::Printing) active=i;
        else if (order.state==OrderState::Waiting) waiting.append(i);
    }
    if (device.busy) {
        const QRectF activeRect(rect.x()+8,rect.y()+72,rect.width()-16,111);
        d.panel(activeRect,printingFill,QColor("#D5E4FA"),6);
        d.panel(QRectF(activeRect.x(),activeRect.y(),88,27),QColor("#203B69"),QColor("#203B69"),3);
        d.text(activeRect.x()+8,activeRect.y()+1,72,25,QStringLiteral("正在打印"),14,Qt::white,true,false,Qt::AlignCenter);
        d.icon(QStringLiteral("document"),activeRect.x()+24,activeRect.y()+41,40);
        const QString title=active>=0 ? state.orders[active].title : QStringLiteral("正在处理打印任务");
        d.text(activeRect.x()+86,activeRect.y()+35,activeRect.width()-98,26,title,14,navy,true);
        d.text(activeRect.x()+86,activeRect.y()+61,108,24,device.jobId,12,secondary);
        const qreal progressX=activeRect.x()+277;
        const qreal progressWidth=activeRect.width()-294;
        const double ratio=device.total>0 ? double(device.printed)/device.total : 0;
        d.text(progressX,activeRect.y()+35,progressWidth-45,26,QStringLiteral("%1 / %2 页").arg(device.printed).arg(device.total),14,navy,false,false,Qt::AlignCenter);
        d.text(activeRect.right()-55,activeRect.y()+35,40,26,QStringLiteral("%1%").arg(qRound(ratio*100)),14,navy,true,false,Qt::AlignRight|Qt::AlignVCenter);
        d.progress(QRectF(progressX,activeRect.y()+65,progressWidth,9),ratio,QColor("#233F6D"));
        const int minutes=std::max(1,int(std::ceil(double(std::max(0,device.total-device.printed))/8.0)));
        d.text(progressX,activeRect.y()+81,progressWidth,23,QStringLiteral("预计 %1 分钟完成").arg(minutes),12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
        if (active>=0) d.hit(activeRect,QStringLiteral("select-order"),QString::number(active),QStringLiteral("查看正在打印的订单 %1").arg(device.jobId));
    } else {
        d.icon(QStringLiteral("printer"),rect.center().x()-18,rect.y()+84,36,QColor("#A9B5CC"));
        d.text(rect.x()+15,rect.y()+124,rect.width()-30,35,!device.online ? QStringLiteral("设备离线，请检查连接")
               : waiting.isEmpty() ? QStringLiteral("空闲，等待新订单") : QStringLiteral("空闲，等待派发"),16,secondary,false,false,Qt::AlignCenter);
    }
    const bool thumbnail=device.color;
    const qreal firstY=rect.y()+(device.busy ? 191 : 180);
    const qreal gap=thumbnail ? 100 : 75;
    const qreal height=thumbnail ? 94 : 69;
    const int maxCards=thumbnail ? 2 : 3;
    const int offset=std::clamp(selection.queueOffset,0,std::max(0,static_cast<int>(waiting.size())-maxCards));
    const QVector<int> visible=waiting.mid(offset,maxCards);
    for (int row=0;row<visible.size();++row)
        queuedCard(d,state,selection,visible[row],QRectF(rect.x()+8,firstY+row*gap,rect.width()-16,height),thumbnail);
    if (waiting.size()>maxCards)
        d.text(rect.right()-172,rect.y()+46,147,16,
               QStringLiteral("第 %1–%2 单 / 共 %3 单").arg(offset+1).arg(offset+visible.size()).arg(waiting.size()),
               10,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    if (waiting.isEmpty() && device.busy) d.text(rect.x()+20,firstY+12,rect.width()-40,55,QStringLiteral("队列为空，等待新订单"),14,secondary,false,false,Qt::AlignCenter);
}

void showcaseQueue(Painter& d, const Snapshot& state, const Selection& selection)
{
    const int pageCount=std::max(1,(int(state.devices.size())+1)/2);
    const int page=std::clamp(selection.device/2,0,pageCount-1);
    const int base=page*2;
    d.text(651,310,142,39,QStringLiteral("第 %1 / %2 页").arg(page+1).arg(pageCount),12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.pageButton(QRectF(807,310,101,39),QStringLiteral("上一页"),QStringLiteral("queue-device-page"),QString::number(base-2),page>0);
    d.pageButton(QRectF(920,310,101,39),QStringLiteral("下一页"),QStringLiteral("queue-device-page"),QString::number(base+2),page+1<pageCount);
    const int black=base<state.devices.size() ? base : -1;
    const int color=base+1<state.devices.size() ? base+1 : -1;
    queueLane(d,state,selection,black,QRectF(39,362,569,413));
    queueLane(d,state,selection,color,QRectF(629,362,495,413));
    QVector<int> unassigned;
    int assigned=0;
    for (int i=0;i<state.orders.size();++i) {
        if (state.orders[i].state!=OrderState::Waiting) continue;
        if (state.orders[i].printer<0) unassigned.append(i); else ++assigned;
    }
    int maxOffset=std::max(0,static_cast<int>(unassigned.size())-1);
    for (int deviceIndex=base;deviceIndex<base+2 && deviceIndex<state.devices.size();++deviceIndex) {
        int waiting=0;
        for (const auto& order : state.orders)
            if (order.printer==deviceIndex && order.state==OrderState::Waiting) ++waiting;
        maxOffset=std::max(maxOffset,waiting-(state.devices[deviceIndex].color ? 2 : 3));
    }
    d.panel(QRectF(39,787,1085,62),Qt::white,ruleColor,4);
    d.text(59,maxOffset>0 ? 792 : 799,199,35,QStringLiteral("未分配  %1 单").arg(unassigned.size()),16,navy,true);
    const int unassignedOffset=std::clamp(selection.queueOffset,0,std::max(0,static_cast<int>(unassigned.size())-1));
    if (maxOffset>0) {
        const QString position=unassigned.size()>1 ? QStringLiteral("第 %1 / %2 单").arg(unassignedOffset+1).arg(unassigned.size()) : QStringLiteral("浏览设备工单");
        d.text(59,823,127,18,position,11,secondary);
        browseArrow(d,QRectF(189,820,28,23),false,QStringLiteral("queue-prev"),std::max(0,selection.queueOffset-1),selection.queueOffset>0,QStringLiteral("上一组队列工单"));
        browseArrow(d,QRectF(227,820,28,23),true,QStringLiteral("queue-next"),selection.queueOffset+1,selection.queueOffset<maxOffset,QStringLiteral("下一组队列工单"));
    }
    verticalRule(d,272,799,38);
    if (!unassigned.isEmpty()) {
        const int index=unassigned[unassignedOffset];
        const Order& order=state.orders[index];
        d.icon(QStringLiteral("document"),302,800,34);
        d.text(364,792,580,30,order.title,14,navy,true);
        d.text(364,819,580,23,order.id+QStringLiteral(" · ")+specs(order),12,secondary);
        d.hit(QRectF(284,788,698,60),QStringLiteral("select-order"),QString::number(index),QStringLiteral("查看未分配订单 %1").arg(order.id));
        d.button(QRectF(996,798,111,39),QStringLiteral("分配设备"),QStringLiteral("assign"),QString::number(index));
    } else d.text(302,798,790,36,QStringLiteral("所有待打印订单均已分配设备"),14,secondary);
    d.icon(QStringLiteral("stack"),44,880,38);
    d.text(102,876,310,30,QStringLiteral("%1 单等待打印").arg(state.waiting),21,navy,true);
    d.text(103,906,315,28,QStringLiteral("%1 单已分配 · %2 单待分配").arg(assigned).arg(unassigned.size()),12,secondary);
    verticalRule(d,437,882,39);
    d.icon(QStringLiteral("clock"),477,881,40);
    d.text(535,876,504,30,QStringLiteral("预计 %1 分钟清空队列").arg(state.clearMinutes),21,navy,true);
    d.text(535,906,504,28,QStringLiteral("按当前设备状态估算"),12,secondary);
}

// The shared backend keeps one waiting order for all printers, in policy order.
void liveWaitingList(Painter& d, const Snapshot& state, const Selection& selection, const QRectF& panel)
{
    d.panel(panel,Qt::white,ruleColor,5);
    const QVector<int> waiting=waitingOrders(state);
    d.text(panel.x()+20,panel.y()+10,200,36,QStringLiteral("等待队列"),16,navy,true);
    d.text(panel.x()+104,panel.y()+12,200,34,QStringLiteral("按派发顺序 · 共 %1 单").arg(waiting.size()),12,secondary);
    constexpr qreal rowHeight=69;
    const qreal top=panel.y()+64;
    const int rows=static_cast<int>((panel.bottom()-8-top)/rowHeight);
    const int offset=std::clamp(selection.queueOffset,0,std::max(0,static_cast<int>(waiting.size())-rows));
    if (waiting.size()>rows) {
        d.text(panel.right()-270,panel.y()+14,180,28,QStringLiteral("第 %1–%2 单").arg(offset+1).arg(std::min(offset+rows,static_cast<int>(waiting.size()))),12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
        browseArrow(d,QRectF(panel.right()-82,panel.y()+17,30,23),false,QStringLiteral("queue-prev"),std::max(0,offset-rows),offset>0,QStringLiteral("上一页等待订单"));
        browseArrow(d,QRectF(panel.right()-44,panel.y()+17,30,23),true,QStringLiteral("queue-next"),offset+rows,offset+rows<waiting.size(),QStringLiteral("下一页等待订单"));
    }
    d.rule(panel.x(),top-8,panel.width());
    if (waiting.isEmpty()) {
        d.emptyState(QRectF(panel.x(),top,panel.width(),panel.bottom()-top),QStringLiteral("document"),
                     state.printing>0 ? QStringLiteral("没有等待中的订单，设备正在处理已派发工单") : QStringLiteral("暂无等待订单"));
        return;
    }
    for (int row=0;row<rows && offset+row<waiting.size();++row) {
        const int index=waiting[offset+row];
        const Order& order=state.orders[index];
        const QRectF rect(panel.x()+8,top+row*rowHeight,panel.width()-16,rowHeight-6);
        const bool selected=selection.order==index;
        d.panel(rect,selected ? selectedFill : Qt::white,selected ? QColor("#F2D3C8") : QColor("#E9ECF1"),4);
        if (selected) selectedEdge(d,rect);
        d.hit(rect,QStringLiteral("select-order"),QString::number(index),QStringLiteral("选择订单 %1 %2").arg(order.id,order.title));
        const QRectF rank(rect.x()+14,rect.center().y()-14,28,28);
        d.panel(rank,offset+row==0 ? navy : Qt::white,offset+row==0 ? navy : QColor("#B9C3D7"),14);
        d.text(rank.x(),rank.y(),rank.width(),rank.height(),QString::number(offset+row+1),13,offset+row==0 ? Qt::white : navy,true,false,Qt::AlignCenter);
        d.text(rect.x()+56,rect.y()+6,290,26,order.title,14,navy,true);
        d.text(rect.x()+56,rect.y()+32,290,22,order.id+QStringLiteral(" · ")+specs(order),12,secondary);
        if (order.service==cps::ServiceTier::Express) serviceBadge(d,QRectF(rect.x()+360,rect.center().y()-14,58,28),order.service);
        else d.text(rect.x()+364,rect.y(),54,rect.height(),serviceLabel(order.service),14,secondary);
        d.text(rect.x()+426,rect.y(),rect.width()-440,rect.height(),waitingText(state,order),12,
               order.blocked.isEmpty() ? secondary : warning,!order.blocked.isEmpty(),false,Qt::AlignRight|Qt::AlignVCenter);
    }
}

void liveDeviceList(Painter& d, const Snapshot& state, const Selection& selection, const QRectF& panel)
{
    d.panel(panel,Qt::white,ruleColor,5);
    const auto online=std::count_if(state.devices.cbegin(),state.devices.cend(),[](const Device& device){return device.online;});
    d.text(panel.x()+20,panel.y()+10,120,36,QStringLiteral("设备"),16,navy,true);
    d.text(panel.x()+64,panel.y()+12,240,34,QStringLiteral("%1 台接单 · 共 %2 台").arg(online).arg(state.devices.size()),12,secondary);
    const qreal top=panel.y()+64;
    d.rule(panel.x(),top-8,panel.width());
    if (state.devices.isEmpty()) {
        d.emptyState(QRectF(panel.x(),top,panel.width(),panel.bottom()-top),QStringLiteral("printer"),QStringLiteral("尚未发现打印设备"));
        return;
    }
    const qreal rowHeight=std::min<qreal>(69,(panel.bottom()-8-top)/static_cast<qreal>(state.devices.size()));
    for (int i=0;i<state.devices.size();++i) {
        const Device& device=state.devices[i];
        const QRectF rect(panel.x()+8,top+i*rowHeight,panel.width()-16,rowHeight-6);
        const bool selected=selection.device==device.index;
        d.panel(rect,selected ? QColor("#F6F8FB") : Qt::white,selected ? QColor("#C9D2E0") : QColor("#E9ECF1"),4);
        d.hit(rect,QStringLiteral("select-device"),QString::number(device.index),device.name);
        d.icon(QStringLiteral("printer"),rect.x()+12,rect.y()+10,30,device.online ? navy : secondary);
        if (device.color) colorDots(d,rect.x()+21,rect.y()+46,3);
        const qreal nameWidth=std::min(QFontMetricsF(merchantFont(14,true)).horizontalAdvance(device.name)+2,qreal(150));
        d.text(rect.x()+56,rect.y()+5,nameWidth,26,device.name,14,navy,true);
        d.dot(rect.x()+56+nameWidth+16,rect.y()+18,deviceStatus(device),device.online ? green : slate);
        d.text(rect.right()-100,rect.y()+5,88,26,QStringLiteral("今日 %1 页").arg(device.todayPages),12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
        if (device.busy) {
            const double ratio=device.total>0 ? double(device.printed)/device.total : 0;
            d.progress(QRectF(rect.x()+56,rect.y()+39,rect.width()-170,6),ratio,QColor("#355784"));
            d.text(rect.right()-108,rect.y()+30,96,24,QStringLiteral("%1 / %2 页").arg(device.printed).arg(device.total),12,navy,false,false,Qt::AlignRight|Qt::AlignVCenter);
        } else d.text(rect.x()+56,rect.y()+30,rect.width()-70,24,capability(device),12,secondary);
    }
}

void liveQueue(Painter& d, const Snapshot& state, const Selection& selection)
{
    QString policy=QStringLiteral("当前策略：%1").arg(policyLabel(state.policy));
    if (state.paused) policy+=QStringLiteral(" · 派发已暂停");
    d.text(630,310,390,39,policy,14,state.paused ? warning : secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    liveWaitingList(d,state,selection,QRectF(39,362,640,487));
    liveDeviceList(d,state,selection,QRectF(695,362,429,487));
    const auto idle=std::count_if(state.devices.cbegin(),state.devices.cend(),[](const Device& device){return device.online && !device.busy;});
    d.icon(QStringLiteral("stack"),44,880,38);
    d.text(102,876,310,30,QStringLiteral("%1 单等待打印").arg(state.waiting),21,navy,true);
    d.text(103,906,315,28,QStringLiteral("%1 单受阻 · %2 台设备空闲").arg(state.blocked).arg(idle),12,state.blocked>0 ? warning : secondary);
    verticalRule(d,437,882,39);
    d.icon(QStringLiteral("clock"),477,881,40);
    const QString clear=state.waiting+state.printing==0 ? QStringLiteral("队列已清空")
        : state.clearMinutes<0 ? QStringLiteral("暂无法估算清空时间") : QStringLiteral("预计 %1 分钟清空队列").arg(state.clearMinutes);
    d.text(535,876,504,30,clear,21,navy,true);
    d.text(535,906,504,28,state.paused ? QStringLiteral("派发暂停期间不计时，恢复后按当前策略继续")
           : QStringLiteral("后台按当前策略与设备推算，仅供参考"),12,secondary);
}
}

void paintWorkbench(Painter& d, const Snapshot& state, const Selection& selection)
{
    hero(d,state,false);
    d.text(39,307,224,54,QStringLiteral("订单工作台"),18,navy,true);
    orderTabs(d,state,selection);
    d.panel(QRectF(796,314,225,38),Qt::white,QColor("#C5CDDC"),8);
    d.icon(QStringLiteral("search"),807,323,22);
    d.hit(QRectF(796,314,225,38),QStringLiteral("search"),{},QStringLiteral("搜索订单或文件名"));
    refreshButton(d,QRectF(1034,314,84,38));
    d.rule(39,364,1079);
    d.text(132,371,330,35,QStringLiteral("订单 / 文件"),14,navy);
    d.text(476,371,217,35,QStringLiteral("打印规格"),14,navy);
    d.text(703,371,100,35,QStringLiteral("服务"),14,navy);
    d.text(832,371,124,35,QStringLiteral("取件时间"),14,navy);
    d.text(1003,371,102,35,QStringLiteral("状态"),14,navy);
    const QVector<int> orders=visibleOrders(state,selection);
    const int offset=std::clamp(selection.offset,0,std::max(0,static_cast<int>(orders.size())-1));
    const QVector<int> visible=orders.mid(offset,5);
    for (int row=0;row<visible.size();++row) {
        const int index=visible[row];
        const Order& order=state.orders[index];
        const qreal y=412+60*row;
        const bool selected=selection.order==index;
        d.panel(QRectF(39,y,1079,59),selected ? selectedFill : Qt::white,QColor("#E4E7ED"),2);
        if (selected) selectedEdge(d,QRectF(39,y,4,59));
        d.hit(QRectF(39,y,1079,59),QStringLiteral("select-order"),QString::number(index),QStringLiteral("选择订单 %1 %2").arg(order.id,order.title));
        d.panel(QRectF(59,y+19,20,20),selected ? red : Qt::white,selected ? red : QColor("#9EADC9"),3);
        if (selected) d.icon(QStringLiteral("check"),61,y+21,16,Qt::white);
        d.icon(QStringLiteral("document"),132,y+15,29);
        d.text(182,y+5,282,28,order.title,14,navy,true);
        d.text(182,y+31,282,24,order.student.isEmpty() ? order.id : order.id+QStringLiteral(" · ")+order.student,14,secondary);
        d.text(476,y+11,216,38,specs(order),14,navy);
        if (order.service==cps::ServiceTier::Express) serviceBadge(d,QRectF(701,y+15,68,30),order.service);
        else d.text(703,y+11,106,38,serviceLabel(order.service),14,navy);
        d.text(832,y+11,160,38,pickup(order),14,navy);
        d.statusBadge(QRectF(1002,y+15,83,30),order.state);
    }
    if (orders.isEmpty())
        d.emptyState(QRectF(39,420,1079,280),selection.search.isEmpty() ? QStringLiteral("document") : QStringLiteral("search"),
                     selection.search.isEmpty() ? QStringLiteral("当前没有%1订单").arg(filterLabel(selection.filter)) : QStringLiteral("没有找到匹配的订单"));
    deviceStrip(d,state,selection);
    if (orders.size()>5) {
        d.text(601,729,239,25,QStringLiteral("第 %1–%2 单 / 共 %3 单").arg(offset+1).arg(offset+visible.size()).arg(orders.size()),
               12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
        browseArrow(d,QRectF(853,730,30,23),false,QStringLiteral("table-prev"),std::max(0,offset-5),offset>0,QStringLiteral("上一页订单"));
        browseArrow(d,QRectF(892,730,30,23),true,QStringLiteral("table-next"),offset+5,offset+5<orders.size(),QStringLiteral("下一页订单"));
    }
    workbenchSidebar(d,state,selection);
    workbenchFooter(d,state,selection);
}

void paintQueue(Painter& d, const Snapshot& state, const Selection& selection)
{
    hero(d,state,true);
    d.text(39,307,400,52,QStringLiteral("打印队列"),18,navy,true);
    d.button({453,310,155,39},QStringLiteral("调度工作台"),"dispatch-open");
    refreshButton(d,QRectF(1034,310,84,39));
    if (state.showcase) showcaseQueue(d,state,selection);
    else liveQueue(d,state,selection);
    queueSidebar(d,state,selection);
}
}
