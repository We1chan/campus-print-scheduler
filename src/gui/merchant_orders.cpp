#include "merchant_painter.hpp"
#include <algorithm>
#include <cmath>

namespace cps::gui::merchant {
namespace {
const QColor secondary = slate;
const QColor selectedFill("#FFF2EC");
const QColor printingFill("#EEF5FF");

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

QString pickup(const Order& order)
{
    return order.pickupTime.isEmpty() ? QStringLiteral("未指定") : order.pickupTime;
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

void serviceBadge(Painter& d, const QRectF& rect, const QString& service)
{
    d.badge(rect,service,service == QStringLiteral("快速"));
}

void metric(Painter& d, qreal x, const QString& label, int value, bool accent=false)
{
    d.text(x,206,150,27,label,14,secondary);
    d.text(x,235,158,43,QString::number(value),32,accent ? red : navy,true);
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
        d.text(59,159,690,37,state.showcase?QStringLiteral("按设备查看进度，拖动工单调整顺序。"):QStringLiteral("按设备查看进度，自动调度等待任务。"),21,secondary);
        int assigned=0, unassigned=0;
        for (const auto& order : state.orders) {
            if (order.state != QStringLiteral("待打印")) continue;
            order.printer >= 0 ? ++assigned : ++unassigned;
        }
        metric(d,60,QStringLiteral("待打印"),state.waiting,true);
        metric(d,274,QStringLiteral("打印中"),state.printing);
        metric(d,503,QStringLiteral("已分配"),assigned);
        metric(d,713,QStringLiteral("未分配"),unassigned);
        verticalRule(d,205,211,56);
        verticalRule(d,431,211,56);
        verticalRule(d,666,211,56);
    } else {
        d.headline(58,81,80,QStringLiteral("每一份，都值得"),QStringLiteral("印好。"));
        d.text(59,159,630,37,QStringLiteral("订单、队列与设备，一眼掌握。"),21,secondary);
        metric(d,59,state.rangeLabel+QStringLiteral("订单"),state.todayOrders);
        metric(d,274,QStringLiteral("待打印"),state.waiting,true);
        metric(d,480,QStringLiteral("打印中"),state.printing);
        metric(d,679,QStringLiteral("待取件"),state.ready);
        verticalRule(d,218,211,56);
        verticalRule(d,426,211,56);
        verticalRule(d,642,211,56);
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

void sidebarHeading(Painter& d, const Order* order, bool queue, bool showcase)
{
    d.text(1175,79,375,52,queue ? QStringLiteral("队列详情") : QStringLiteral("订单详情"),18,navy,true);
    if (!order) {
        d.text(1175,140,370,36,QStringLiteral("暂无订单"),18,secondary);
        d.icon(QStringLiteral("document"),1321,267,70,ruleColor);
        d.text(1175,368,374,36,QStringLiteral("选择订单查看详情"),16,secondary,false,false,Qt::AlignCenter);
        return;
    }
    d.text(1175,134,78,30,order->id,16,secondary);
    d.badge(QRectF(1250,134,83,29),order->state);
    if(showcase)documentPreview(d,*order,QRectF(1238,144,264,319));
    else {d.panel({1238,144,264,319},Qt::white,ruleColor,4);d.icon(QStringLiteral("document"),1340,209,58,secondary);d.text(1254,287,230,36,QStringLiteral("文件元数据"),16,navy,true,false,Qt::AlignCenter);d.text(1254,330,230,33,QStringLiteral("原件尚未传输"),14,secondary,false,false,Qt::AlignCenter);}
    d.text(1175,486,378,40,order->title,16,navy,true);
}

void workbenchSidebar(Painter& d, const Snapshot& state, const Selection& selection)
{
    const Order* order=selectedOrder(state,selection);
    sidebarHeading(d,order,false,state.showcase);
    if (!order) return;
    d.panel(QRectF(1303,446,124,35),Qt::white,ruleColor,18);
    d.icon(QStringLiteral("search"),1325,452,20);
    d.text(1350,446,67,35,QStringLiteral("查看文件"),14,navy,true);
    d.hit(QRectF(1303,446,124,35),QStringLiteral("view-file"),QString::number(selection.order),QStringLiteral("查看文件"));
    d.rule(1175,535,378);
    d.text(1175,545,350,29,QStringLiteral("打印参数"),16,navy,true);
    d.detailRow(576,QStringLiteral("纸张"),order->a3 ? QStringLiteral("A3") : QStringLiteral("A4"));
    d.detailRow(606,QStringLiteral("页数 / 份数"),QStringLiteral("%1 页 / %2 份").arg(order->pages).arg(order->copies));
    d.detailRow(636,QStringLiteral("色彩 / 单双面"),QStringLiteral("%1 / %2").arg(order->color ? QStringLiteral("彩色") : QStringLiteral("黑白"),order->duplex ? QStringLiteral("双面") : QStringLiteral("单面")));
    d.detailRow(666,QStringLiteral("打印服务"),order->service);
    d.detailRow(696,QStringLiteral("订单金额"),money(order->amountCents));
    d.detailRow(726,QStringLiteral("取件时间"),pickup(*order)+QStringLiteral(" 前"));
    d.rule(1175,760,378);
    d.text(1175,781,105,29,QStringLiteral("分配打印机"),14,navy,true);
    const Device* device=selectedDevice(state,selection);
    const QString name=device ? device->name : QStringLiteral("选择设备");
    d.panel(QRectF(1274,777,162,38),Qt::white,QColor("#B9C3D7"),5);
    d.text(1286,780,129,32,name,14,navy,true);
    d.text(1410,780,22,32,QStringLiteral("⌄"),16,navy);
    const int next=state.devices.isEmpty() ? -1 : (selection.device+1)%state.devices.size();
    d.hit(QRectF(1274,777,162,38),QStringLiteral("select-device"),QString::number(next),QStringLiteral("选择打印设备"));
    d.dot(1462,794,device && device->online ? QStringLiteral("设备在线") : QStringLiteral("设备离线"),device && device->online ? green : slate);
    d.text(1450,806,112,23,device && device->paper > 0 ? QStringLiteral("纸张充足，可开始打印") : QStringLiteral("请检查设备纸张"),11,secondary);
}

void queueSidebar(Painter& d, const Snapshot& state, const Selection& selection)
{
    const Order* order=selectedOrder(state,selection);
    sidebarHeading(d,order,true,state.showcase);
    if (!order) return;
    d.rule(1175,525,378);
    const Device* device=order->printer>=0 && order->printer<state.devices.size() ? &state.devices[order->printer] : nullptr;
    int position=0;
    for (int i=0;i<state.orders.size();++i) {
        if (state.orders[i].printer==order->printer && state.orders[i].state==QStringLiteral("待打印")) ++position;
        if (i==selection.order) break;
    }
    const QString location=device ? QStringLiteral("%1队列第 %2 单").arg(device->color ? QStringLiteral("彩色") : QStringLiteral("黑白")).arg(std::max(1,position)) : QStringLiteral("等待分配设备");
    d.detailRow(543,QStringLiteral("队列位置"),location);
    d.detailRow(576,QStringLiteral("打印规格"),QStringLiteral("%1 / %2 / %3").arg(order->a3 ? QStringLiteral("A3") : QStringLiteral("A4"),order->color ? QStringLiteral("彩色") : QStringLiteral("黑白"),order->duplex ? QStringLiteral("双面") : QStringLiteral("单面")));
    d.detailRow(609,QStringLiteral("页数 / 份数"),QStringLiteral("%1 页 / %2 份").arg(order->pages).arg(order->copies));
    d.detailRow(642,QStringLiteral("打印服务"),order->service);
    d.detailRow(675,QStringLiteral("取件时间"),pickup(*order)+QStringLiteral(" 前"));
    d.detailRow(708,QStringLiteral("分配设备"),device ? device->name : QStringLiteral("未分配"));
    d.rule(1175,750,378);
    d.button(QRectF(1175,768,179,46),QStringLiteral("↕  调整顺序"),QStringLiteral("reorder"),QString::number(selection.order));
    d.button(QRectF(1374,768,177,46),QStringLiteral("⇄  转移设备"),QStringLiteral("transfer"),QString::number(selection.order));
}

void deviceStrip(Painter& d, const Snapshot& state, const Selection& selection)
{
    d.text(39,726,400,30,QStringLiteral("设备与队列"),18,navy,true);
    d.text(958,729,158,27,QStringLiteral("查看全部设备  ›"),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.hit(QRectF(957,727,163,28),QStringLiteral("page"),QStringLiteral("devices"),QStringLiteral("查看全部设备"));
    const int base=std::max(0,selection.device/2*2);
    for (int lane=0;lane<2 && base+lane<state.devices.size();++lane) {
        const int i=base+lane;
        const Device& device=state.devices[i];
        const qreal x=lane==0 ? 39 : 587;
        d.panel(QRectF(x,762,536,90),Qt::white,ruleColor,4);
        d.hit(QRectF(x,762,536,90),QStringLiteral("select-device"),QString::number(i),device.name);
        d.icon(QStringLiteral("printer"),x+24,780,46);
        if (device.color) {
            d.p.save(); d.p.setPen(Qt::NoPen);
            const QColor colors[]={QColor("#16A3DE"),red,QColor("#F2C000")};
            for (int c=0;c<3;++c) { d.p.setBrush(colors[c]); d.p.drawEllipse(QPointF(x+37+c*9,821),4.4,4.4); }
            d.p.restore();
        }
        d.text(x+94,775,191,26,device.name,14,navy,true);
        d.dot(x+233,789,device.online ? (device.busy ? QStringLiteral("打印中") : QStringLiteral("空闲")) : QStringLiteral("离线"),device.online ? green : slate);
        const double ratio=device.total>0 ? double(device.printed)/device.total : 0;
        d.progress(QRectF(x+94,807,300,8),ratio,QColor("#355784"));
        d.text(x+94,821,310,24,device.busy ? QStringLiteral("%1 · %2 / %3 页").arg(device.jobId).arg(device.printed).arg(device.total) : QStringLiteral("可接收新任务"),12,secondary);
        verticalRule(d,x+426,782,51);
        d.text(x+455,781,69,25,QStringLiteral("队列"),14,secondary);
        d.text(x+455,807,69,28,QStringLiteral("%1 单").arg(device.queued),14,navy,true);
    }
    if (state.devices.isEmpty()) d.text(39,773,1081,63,QStringLiteral("尚未发现打印设备"),16,secondary,false,false,Qt::AlignCenter);
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
    const QString forecast=order&&(order->state==QStringLiteral("已完成")||order->state==QStringLiteral("待取件"))?QStringLiteral("打印已完成"):minutes<0?QStringLiteral("暂无法估算完成时间"):QStringLiteral("预计 %1 分钟完成").arg(minutes);
    d.text(534,876,500,30,forecast,21,navy,true);
    d.text(535,907,495,26,!state.showcase?QStringLiteral("由后台自动匹配设备"):device ? QStringLiteral("发送至 %1").arg(device->name) : QStringLiteral("请先分配打印设备"),12,secondary);
    d.button(QRectF(1078,875,188,54),QStringLiteral("打印取件单"),QStringLiteral("pickup-slip"),QString::number(selection.order));
    if(order&&order->state==QStringLiteral("待取件"))d.button(QRectF(1286,875,262,54),QStringLiteral("确认取件  →"),QStringLiteral("pickup"),order->id,true);
    else if(order&&(order->state==QStringLiteral("已完成")||order->state==QStringLiteral("已取消")))d.text(1286,875,262,54,order->state,18,secondary,true,false,Qt::AlignCenter);
    else d.button(QRectF(1286,875,262,54),QStringLiteral("开始打印  →"),QStringLiteral("start"),QString::number(selection.device),true);
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
    if (device.color) {
        d.p.save(); d.p.setPen(Qt::NoPen);
        const QColor colors[]={QColor("#16A3DE"),red,QColor("#F2C000")};
        for (int c=0;c<3;++c) { d.p.setBrush(colors[c]); d.p.drawEllipse(QPointF(rect.x()+32+c*9,rect.y()+50),4,4); }
        d.p.restore();
    }
    d.text(rect.x()+82,rect.y()+16,190,31,device.name,16,navy,true);
    d.dot(rect.x()+225,rect.y()+32,device.online ? (device.busy ? QStringLiteral("打印中") : QStringLiteral("空闲")) : QStringLiteral("离线"),device.online ? green : slate);
    d.text(rect.right()-113,rect.y()+17,88,30,QStringLiteral("%1 单等待").arg(device.queued),14,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    d.rule(rect.x(),rect.y()+63,rect.width());
    QVector<int> waiting;
    int active=-1;
    for (int i=0;i<state.orders.size();++i) {
        const Order& order=state.orders[i];
        if (order.printer!=deviceIndex) continue;
        if (order.state==QStringLiteral("打印中")) active=i;
        else if (order.state==QStringLiteral("待打印")) waiting.append(i);
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
        const int minutes=state.showcase?std::max(1,int(std::ceil(double(std::max(0,device.total-device.printed))/8.0))):active>=0?state.orders[active].estimatedMinutes:-1;
        d.text(progressX,activeRect.y()+81,progressWidth,23,minutes<0?QStringLiteral("暂无法估算完成时间"):QStringLiteral("预计 %1 分钟完成").arg(minutes),12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
        if (active>=0) d.hit(activeRect,QStringLiteral("select-order"),QString::number(active),QStringLiteral("查看正在打印的订单 %1").arg(device.jobId));
    } else {
        d.icon(QStringLiteral("printer"),rect.center().x()-18,rect.y()+84,36,QColor("#A9B5CC"));
        d.text(rect.x()+15,rect.y()+124,rect.width()-30,35,device.online ? QStringLiteral("准备就绪，可启动队列") : QStringLiteral("设备离线，请检查连接"),16,secondary,false,false,Qt::AlignCenter);
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
    if (waiting.isEmpty()) d.text(rect.x()+20,firstY+12,rect.width()-40,55,QStringLiteral("队列为空，等待新订单"),14,secondary,false,false,Qt::AlignCenter);
}
}

void paintWorkbench(Painter& d, const Snapshot& state, const Selection& selection)
{
    hero(d,state,false);
    d.text(39,307,224,54,QStringLiteral("订单工作台"),18,navy,true);
    const QString labels[]={QStringLiteral("待打印"),QStringLiteral("打印中"),QStringLiteral("待取件"),QStringLiteral("已完成")};
    const int values[]={state.waiting,state.printing,state.ready,state.completed};
    const qreal tabX[]={267,382,497,627};
    for (int i=0;i<4;++i) {
        const QRectF rect(tabX[i],311,i==3 ? 113 : 98,48);
        d.text(rect.x()+17,315,70,31,labels[i],14,navy);
        d.text(rect.x()+71,315,34,31,QString::number(values[i]),12,i==0 ? red : secondary);
        d.hit(rect,QStringLiteral("filter"),QString::number(i),QStringLiteral("查看%1订单").arg(labels[i]));
        if (selection.filter==i) { d.p.save(); d.p.setPen(QPen(red,4)); d.p.drawLine(QPointF(rect.x(),358),QPointF(rect.right(),358)); d.p.restore(); }
    }
    d.panel(QRectF(796,314,225,38),Qt::white,QColor("#C5CDDC"),8);
    d.icon(QStringLiteral("search"),807,323,22);
    d.hit(QRectF(796,314,225,38),QStringLiteral("search"),{},QStringLiteral("搜索订单或文件名"));
    refreshButton(d,QRectF(1034,314,84,38));
    d.rule(39,364,1079);
    d.panel(QRectF(59,378,20,20),Qt::white,QColor("#9EADC9"),3);
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
        d.text(182,y+31,282,24,order.id+QStringLiteral(" · ")+order.student,14,secondary);
        d.text(476,y+11,216,38,specs(order),14,navy);
        if (order.service==QStringLiteral("快速")) serviceBadge(d,QRectF(701,y+15,68,30),order.service);
        else d.text(703,y+11,106,38,order.service,14,navy);
        d.text(832,y+11,142,38,pickup(order),14,navy);
        d.badge(QRectF(1002,y+15,83,30),order.state);
    }
    if (orders.isEmpty()) d.text(39,457,1079,168,selection.search.isEmpty() ? QStringLiteral("当前没有%1订单").arg(labels[std::clamp(selection.filter,0,3)]) : QStringLiteral("没有找到匹配的订单"),16,secondary,false,false,Qt::AlignCenter);
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
    d.text(39,307,520,52,QStringLiteral("打印队列"),18,navy,true);
    const int pageCount=std::max(1,(int(state.devices.size())+1)/2);
    const int page=std::clamp(selection.device/2,0,pageCount-1);
    const int base=page*2;
    d.text(651,310,142,39,QStringLiteral("第 %1 / %2 页").arg(page+1).arg(pageCount),12,secondary,false,false,Qt::AlignRight|Qt::AlignVCenter);
    const auto pageButton=[&](QRectF rect,const QString& label,int target,bool enabled){
        if(enabled)d.button(rect,label,QStringLiteral("queue-device-page"),QString::number(target));
        else{d.panel(rect,QColor("#F7F8FA"),ruleColor,7);d.text(rect.x(),rect.y(),rect.width(),rect.height(),label,14,secondary,false,false,Qt::AlignCenter);}
    };
    pageButton(QRectF(807,310,101,39),QStringLiteral("上一页"),base-2,page>0);
    pageButton(QRectF(920,310,101,39),QStringLiteral("下一页"),base+2,page+1<pageCount);
    refreshButton(d,QRectF(1034,310,84,39));
    const int black=base<state.devices.size() ? base : -1;
    const int color=base+1<state.devices.size() ? base+1 : -1;
    queueLane(d,state,selection,black,QRectF(39,362,569,413));
    queueLane(d,state,selection,color,QRectF(629,362,495,413));
    QVector<int> unassigned;
    int assigned=0;
    for (int i=0;i<state.orders.size();++i) {
        if (state.orders[i].state!=QStringLiteral("待打印")) continue;
        if (state.orders[i].printer<0) unassigned.append(i); else ++assigned;
    }
    int maxOffset=std::max(0,static_cast<int>(unassigned.size())-1);
    for (int deviceIndex=base;deviceIndex<base+2 && deviceIndex<state.devices.size();++deviceIndex) {
        int waiting=0;
        for (const auto& order : state.orders)
            if (order.printer==deviceIndex && order.state==QStringLiteral("待打印")) ++waiting;
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
    queueSidebar(d,state,selection);
    d.icon(QStringLiteral("stack"),44,880,38);
    d.text(102,876,310,30,QStringLiteral("%1 单等待打印").arg(state.waiting),21,navy,true);
    d.text(103,906,315,28,QStringLiteral("%1 单已分配 · %2 单待分配").arg(assigned).arg(unassigned.size()),12,secondary);
    verticalRule(d,437,882,39);
    d.icon(QStringLiteral("clock"),477,881,40);
    d.text(535,876,504,30,state.clearMinutes<0?QStringLiteral("暂无法估算清空时间"):QStringLiteral("预计 %1 分钟清空队列").arg(state.clearMinutes),21,navy,true);
    d.text(535,906,504,28,QStringLiteral("按当前设备状态估算"),12,secondary);
    const bool blackPaused=black>=0 && !state.devices[black].enabled;
    d.button(QRectF(1078,875,193,54),blackPaused ? QStringLiteral("启动黑白队列") : QStringLiteral("暂停黑白队列"),blackPaused ? QStringLiteral("start") : QStringLiteral("pause"),QString::number(black));
    d.button(QRectF(1287,875,261,54),QStringLiteral("▷  启动彩色队列  →"),QStringLiteral("start"),QString::number(color),true);
}
}
