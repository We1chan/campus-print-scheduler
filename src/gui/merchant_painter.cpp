#include "merchant_painter.hpp"
#include <QFontMetricsF>
#include <QPainterPath>
#include <QRandomGenerator>
#include <algorithm>
#include <array>
#include <cmath>
static void initializeMerchantArt() { Q_INIT_RESOURCE(merchant_assets); }
namespace cps::gui::merchant {
Assets::Assets() {
    initializeMerchantArt();
    workbench.load(":/merchant/workbench.png"); queue.load(":/merchant/queue.png");
    devices.load(":/merchant/devices.png"); stats.load(":/merchant/stats.png");
    // A text-free material swatch; application content is never an image layer.
    texture=workbench.copy(700,76,128,128);
    // A single photographic plate keeps the paper's light, texture and shadows
    // continuous. Only live text/metrics are painted above it, never UI pixels.
    const QImage paper(":/merchant/hero-paper-v10.png");
    paperBackground=paper.copy(0,56,1134,236);
    const QImage dispatch(":/merchant/dispatch-paper-v10.png");
    dispatchBackground=dispatch.copy(0,56,1586,162);
}
void Painter::text(qreal x,qreal y,qreal w,qreal h,const QString& value,int size,QColor color,bool bold,bool serif,int align) {
    QFont font=merchantFont(size,bold,serif);
    if(serif && size>=50) {
        const qreal advance=QFontMetricsF(font).horizontalAdvance(value);
        if(advance>w)font.setPixelSize(std::max(1,int(std::floor(size*w/advance))));
    }
    p.setFont(font); p.setPen(color);
    p.drawText(QRectF(x,y,w,h),align,QFontMetricsF(font).elidedText(value,Qt::ElideRight,w));
}
void Painter::navigationText(QRectF rect,const QString& value) {
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(18);font.setBold(false);
    p.setFont(font);p.setPen(QColor("#17233E"));
    p.drawText(rect,Qt::AlignCenter,value);
}
void Painter::headline(qreal x,qreal y,qreal h,const QString& prefix,const QString& accent) {
    const QFont font=merchantFont(60,true,true);
    const QFontMetricsF metrics(font);
    const qreal prefixWidth=metrics.horizontalAdvance(prefix);
    const qreal accentWidth=metrics.horizontalAdvance(accent);
    // Place the accent at the measured text baseline.
    p.setFont(font);p.setPen(navy);
    p.drawText(QRectF(x,y,prefixWidth+2,h),Qt::AlignLeft|Qt::AlignVCenter,prefix);
    p.setPen(red);
    p.drawText(QRectF(x+prefixWidth,y,accentWidth+2,h),Qt::AlignLeft|Qt::AlignVCenter,accent);
}
void Painter::rule(qreal x,qreal y,qreal w) {p.setPen(QPen(ruleColor,1)); p.drawLine(QPointF(x,y),QPointF(x+w,y));}
void Painter::heroMetric(int column,const QString& label,const QString& value,QColor color,const QString& unit) {
    const std::array<qreal,4> xs{{59,274,503,713}},widths{{146,157,163,112}};
    if(column<0||column>=4)return;
    const qreal x=xs[static_cast<std::size_t>(column)],width=widths[static_cast<std::size_t>(column)];
    text(x,206,width,27,label,14,slate);
    const QFont unitFont=merchantFont(14,true);
    const QFontMetricsF unitMetrics(unitFont);
    const qreal unitWidth=unit.isEmpty()?0:unitMetrics.horizontalAdvance(unit)+8;
    const qreal valueWidth=width-unitWidth;
    QFont valueFont=merchantFont(32,true);
    while(valueFont.pixelSize()>1&&QFontMetricsF(valueFont).horizontalAdvance(value)>valueWidth)valueFont.setPixelSize(valueFont.pixelSize()-1);
    text(x,235,valueWidth,43,value,valueFont.pixelSize(),color,true);
    if(!unit.isEmpty()){
        const QFontMetricsF metrics(valueFont);
        const qreal baselineOffset=(metrics.ascent()-metrics.descent()-unitMetrics.ascent()+unitMetrics.descent())/2;
        text(x+metrics.horizontalAdvance(value)+8,235+baselineOffset,unitWidth,43,unit,14,color,true);
    }
    if(column<3){const std::array<qreal,3> dividers{{205,431,666}};p.save();p.setPen(QPen(ruleColor,1.4));p.drawLine(QPointF(dividers[static_cast<std::size_t>(column)],211),QPointF(dividers[static_cast<std::size_t>(column)],267));p.restore();}
}
void Painter::panel(QRectF r,QColor fill,QColor border,qreal radius) { p.setPen(QPen(border,1));p.setBrush(fill);p.drawRoundedRect(r,radius,radius);p.setBrush(Qt::NoBrush); }
void Painter::hit(QRectF r,const QString& action,const QString& value,const QString& label) {hits.push_back({r,action,value,label.isEmpty()?action:label});}
void Painter::button(QRectF r,const QString& label,const QString& action,const QString& value,bool primary) {
    const bool enabled=showcase_||!requiresShowcase(action);
    const QColor fill=enabled?(primary?red:QColor(Qt::white)):(primary?QColor("#CFA097"):QColor("#F1F0ED"));
    panel(r,fill,enabled?(primary?red:QColor("#D9DCE2")):(primary?fill:QColor("#E2E0DB")),6);
    const int size=primary?21:16;
    const QColor ink=enabled?(primary?QColor(Qt::white):navy):(primary?QColor("#FFF7F4"):QColor("#A2A7B0"));
    text(r.x()+12,r.y(),r.width()-24,r.height(),label,size,ink,primary,false,Qt::AlignCenter);
    hits.push_back({r,action,value,label,true,primary});
}
void Painter::pageButton(QRectF r,const QString& label,const QString& action,const QString& value,bool enabled) {
    if(enabled){button(r,label,action,value);return;}
    panel(r,QColor("#F7F8FA"),ruleColor,7);
    text(r.x(),r.y(),r.width(),r.height(),label,14,slate,false,false,Qt::AlignCenter);
    hits.push_back({r,action,value,label,false,false,false});
}
void Painter::badge(QRectF r,const QString& label,bool accent) {
    panel(r,accent?QColor("#FFF9F7"):QColor("#FAFBFD"),accent?red:QColor("#ADB7C8"),r.height()/2);
    text(r.x()+5,r.y(),r.width()-10,r.height(),label,15,accent?red:navy,false,false,Qt::AlignCenter);
}
void Painter::progress(QRectF r,double value,QColor color) {
    p.setPen(Qt::NoPen);p.setBrush(QColor("#E1E5EE"));p.drawRoundedRect(r,r.height()/2,r.height()/2);
    p.setBrush(color); auto f=r;f.setWidth(r.width()*std::clamp(value,0.,1.));
    if(f.width()>0) p.drawRoundedRect(f,r.height()/2,r.height()/2);
    p.setBrush(Qt::NoBrush);
}
void Painter::dot(qreal x,qreal y,const QString& label,QColor color) {
    p.setPen(Qt::NoPen);p.setBrush(color);p.drawEllipse(QPointF(x,y),4,4);p.setBrush(Qt::NoBrush);
    text(x+15,y-14,220,28,label,14,color);
}
void Painter::art(const QString& name,QRectF target) {
    if(name=="printer-black")p.drawImage(target,assets.devices,QRectF(150,402,297,179));
    else if(name=="printer-color")p.drawImage(target,assets.devices,QRectF(735,392,251,185));
    else if(name=="coffee")p.drawImage(target,assets.heroCoffee);
    else if(name=="leaf")p.drawImage(target,assets.heroLeaf);
    else if(name=="poster")p.drawImage(target,assets.queue,QRectF(695,646,53,73));
}
void Painter::cover(QRectF target) {
    p.save();QPainterPath clip;clip.addRect(target);
    QPainterPath oldStatus;oldStatus.addRect(QRectF(target.x()+12*target.width()/265.,target.y(),83*target.width()/265.,22*target.height()/317.));
    p.setClipPath(clip.subtracted(oldStatus));p.drawImage(target,assets.queue,QRectF(1238,143,265,317));p.restore();
}
void Painter::detailRow(qreal y,const QString& key,const QString& value,QColor color) {
    text(1175,y,168,30,key,14,slate);text(1338,y,216,30,value,14,color,false,false,Qt::AlignRight|Qt::AlignVCenter);
}
void Painter::icon(const QString& name,qreal x,qreal y,qreal size,QColor color) {
    p.save();p.translate(x,y);p.scale(size/32.,size/32.);p.setPen(QPen(color,2.,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    if(name=="document"||name=="stack") {
        QPainterPath q;q.moveTo(7,2);q.lineTo(21,2);q.lineTo(28,9);q.lineTo(28,29);q.lineTo(7,29);q.closeSubpath();p.drawPath(q);
        p.drawLine(21,2,21,10);p.drawLine(21,10,28,10);p.drawLine(12,16,23,16);p.drawLine(12,21,23,21);
        if(name=="stack") {p.drawLine(3,5,3,28);p.drawLine(3,28,5,28);}
    } else if(name=="printer") {
        p.drawRoundedRect(QRectF(3,11,26,14),2,2);p.drawRect(QRectF(8,2,16,9));p.drawRect(QRectF(9,20,14,10));p.drawPoint(24,16);
    } else if(name=="clock") {p.drawEllipse(QRectF(2,2,28,28));p.drawLine(16,6,16,16);p.drawLine(16,16,23,20);
    } else if(name=="search") {p.drawEllipse(QRectF(3,3,18,18));p.drawLine(19,19,29,29);
    } else if(name=="arrow") {p.drawLine(3,16,29,16);p.drawLine(21,8,29,16);p.drawLine(21,24,29,16);
    } else if(name=="check") {p.drawLine(5,16,13,23);p.drawLine(13,23,27,8);
    } else if(name=="grip") {p.setPen(Qt::NoPen);p.setBrush(color);for(int j=0;j<3;++j)for(int i=0;i<2;++i)p.drawEllipse(QPointF(10+i*8,8+j*8),1.6,1.6);
    } else if(name=="refresh") {p.drawArc(QRectF(4,4,24,24),35*16,295*16);p.drawLine(26,3,26,10);p.drawLine(19,10,26,10);
    } else if(name=="calendar") {p.drawRoundedRect(QRectF(3,6,26,24),2,2);p.drawLine(3,13,29,13);p.drawLine(10,2,10,9);p.drawLine(23,2,23,9);p.drawPoint(11,19);p.drawPoint(20,19);p.drawPoint(11,25);p.drawPoint(20,25);
    } else if(name=="pause") {p.drawEllipse(QRectF(2,2,28,28));p.drawLine(12,10,12,22);p.drawLine(20,10,20,22);
    } else if(name=="play") {QPainterPath a;a.moveTo(8,3);a.lineTo(28,16);a.lineTo(8,29);a.closeSubpath();p.drawPath(a);
    } else if(name=="shop") {p.drawRect(QRectF(6,13,20,16));p.drawRect(QRectF(13,20,7,9));QPainterPath q;q.moveTo(4,12);q.lineTo(8,3);q.lineTo(24,3);q.lineTo(28,12);q.closeSubpath();p.drawPath(q);p.drawLine(4,12,28,12);
    } else {p.drawEllipse(QRectF(4,4,24,24));p.drawLine(9,16,23,16);p.drawLine(16,9,16,23);}
    p.restore();
}
QString money(qint64 cents) {return QStringLiteral("¥%1").arg(cents/100.,0,'f',2);}
QString specs(const Order& o,bool compact) {
    const QString s=QStringLiteral("%1 页 / %2 / %3 / %4").arg(o.pages).arg(o.a3?"A3":"A4").arg(o.color?QStringLiteral("彩色"):QStringLiteral("黑白")).arg(o.duplex?QStringLiteral("双面"):QStringLiteral("单面"));
    return compact?s: (o.copies>1?s+QStringLiteral(" / %1 份").arg(o.copies):s);
}
QVector<int> visibleOrders(const Snapshot& s,const Selection& sel) {
    QVector<int> result;const QStringList states{QStringLiteral("待打印"),QStringLiteral("打印中"),QStringLiteral("待取件"),QStringLiteral("已完成")};
    for(int i=0;i<s.orders.size();++i) {const auto& o=s.orders[i];if(sel.filter>=0&&sel.filter<4&&o.state!=states[sel.filter])continue;
        if(!sel.search.isEmpty()&&!o.title.contains(sel.search,Qt::CaseInsensitive)&&!o.id.contains(sel.search,Qt::CaseInsensitive)&&!o.student.contains(sel.search))continue;
        result.push_back(i);}
    std::stable_sort(result.begin(),result.end(),[&s](int a,int b){return s.orders[a].id<s.orders[b].id;});
    return result;
}
}
