#pragma once
#include "merchant_data.hpp"
#include <QPainter>
#include <QImage>
#include <QRectF>
#include <QFont>
namespace cps::gui::merchant {
// Student editorial palette: strong text, action accent, secondary text, rules and availability.
inline const QColor navy("#14213E"), red("#D63D29"), slate("#69748A"), ruleColor("#DADDE2"), green("#138342");
// Match the student client: interface text is YaHei; the editorial headline
// and text printed on the document itself are SimSun.
inline QFont merchantFont(int size, bool bold = false, bool serif = false) {
    QFont font(serif ? QStringLiteral("SimSun") : QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(size);
    font.setWeight(bold?(!serif&&size<=22?QFont::DemiBold:QFont::Bold):QFont::Normal);
    return font;
}
struct Hit { QRectF rect; QString action, value, label; bool button = false, primary = false, enabled = true; };
inline bool requiresShowcase(const QString& action) {
    return action=="assign"||action=="transfer"||action=="reorder"||action=="test-page";
}
struct Selection { int order = 0, device = 0, filter = 0, offset = 0, queueOffset = 0; QString search, range = QStringLiteral("今天"); };
struct Assets {
    QImage workbench, queue, devices, stats, texture;
    Assets();
};
class Painter {
public:
    Painter(QPainter& painter, QVector<Hit>& controls, const Assets& art, bool showcase=true) : p(painter), hits(controls), assets(art), showcase_(showcase) {}
    QPainter& p; QVector<Hit>& hits; const Assets& assets;
    void text(qreal x,qreal y,qreal w,qreal h,const QString& value,int size=14,QColor color=navy,bool bold=false,bool serif=false,int align=Qt::AlignLeft|Qt::AlignVCenter);
    void headline(qreal x,qreal y,qreal h,const QString& prefix,const QString& accent);
    void navigationText(QRectF rect,const QString& value);
    void rule(qreal x,qreal y,qreal w);
    void panel(QRectF rect,QColor fill=Qt::white,QColor border=ruleColor,qreal radius=6);
    void hit(QRectF rect,const QString& action,const QString& value={},const QString& label={});
    void button(QRectF rect,const QString& label,const QString& action,const QString& value={},bool primary=false);
    void pageButton(QRectF rect,const QString& label,const QString& action,const QString& value,bool enabled);
    void icon(const QString& name,qreal x,qreal y,qreal size=26,QColor color=navy);
    void badge(QRectF rect,const QString& label,bool accent=false);
    void progress(QRectF rect,double value,QColor color=navy);
    void dot(qreal x,qreal y,const QString& label,QColor color=green);
    void art(const QString& name,QRectF rect);
    void cover(QRectF rect);
    void detailRow(qreal y,const QString& key,const QString& value,QColor color=navy);
private:
    bool showcase_;
};
QString specs(const Order& order,bool compact=false);
QString money(qint64 cents);
QVector<int> visibleOrders(const Snapshot& state,const Selection& selection);
void paintWorkbench(Painter& d,const Snapshot& state,const Selection& selection);
void paintQueue(Painter& d,const Snapshot& state,const Selection& selection);
void paintDevices(Painter& d,const Snapshot& state,const Selection& selection);
void paintStats(Painter& d,const Snapshot& state,const Selection& selection);
}
