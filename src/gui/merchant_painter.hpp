#pragma once
#include "merchant_data.hpp"
#include <QPainter>
#include <QImage>
#include <QRectF>
#include <QFont>
#include <QCache>
namespace cps::gui::merchant {
// Student editorial palette: strong text, action accent, secondary text, rules and availability.
inline const QColor navy("#14213E"), red("#D63D29"), slate("#69748A"), ruleColor("#DADDE2"), green("#138342");
// Every page is drawn on this reference canvas, uniformly scaled into the window.
inline constexpr qreal canvasWidth=1586, canvasHeight=992;
inline constexpr qreal heroBottom=292;
// Match the student client: interface text is YaHei; the editorial headline
// and text printed on the document itself are SimSun.
inline QFont merchantFont(int size, bool bold = false, bool serif = false) {
    QFont font(serif ? QStringLiteral("SimSun") : QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(size);
    font.setWeight(bold?(!serif&&size<=22?QFont::DemiBold:QFont::Bold):QFont::Normal);
    return font;
}
// `rect` is in reference-canvas units and drives the button's own painting;
// `bounds` is the same area in widget pixels, where the button is placed.
struct Hit { QRectF rect; QString action, value, label; bool button = false, primary = false, enabled = true; QRectF bounds = {}; };
inline bool requiresShowcase(const QString& action) {
    return action=="assign"||action=="transfer"||action=="reorder"||action=="test-page";
}
// Values double as `filter` hit values, so object names stay stable across modes.
enum class OrderFilter { Waiting, Printing, Ready, Completed, Closed, All };
QVector<OrderFilter> orderFilters(bool showcase);
QString filterLabel(OrderFilter filter);
struct Selection { int order = 0, device = 0, offset = 0, queueOffset = 0; OrderFilter filter = OrderFilter::Waiting; QString search, range = QStringLiteral("今天"); };
struct Assets {
    QImage workbench, queue, devices, stats, texture, heroCoffee, heroLeaf, paperBackground, dispatchBackground;
    Assets();
    QImage orderCover(const QString& path) const;
    mutable QCache<QString,QImage> coverCache{8192};
};
class Painter {
public:
    Painter(QPainter& painter, QVector<Hit>& controls, const Assets& art, bool showcase=true) : p(painter), hits(controls), assets(art), showcase_(showcase) {}
    QPainter& p; QVector<Hit>& hits; const Assets& assets;
    void text(qreal x,qreal y,qreal w,qreal h,const QString& value,int size=14,QColor color=navy,bool bold=false,bool serif=false,int align=Qt::AlignLeft|Qt::AlignVCenter);
    void headline(qreal x,qreal y,qreal h,const QString& prefix,const QString& accent);
    void heroMetric(int column,const QString& label,const QString& value,QColor color=navy,const QString& unit={});
    void navigationText(QRectF rect,const QString& value);
    void rule(qreal x,qreal y,qreal w);
    void panel(QRectF rect,QColor fill=Qt::white,QColor border=ruleColor,qreal radius=6);
    void hit(QRectF rect,const QString& action,const QString& value={},const QString& label={});
    void button(QRectF rect,const QString& label,const QString& action,const QString& value={},bool primary=false);
    void pageButton(QRectF rect,const QString& label,const QString& action,const QString& value,bool enabled);
    void icon(const QString& name,qreal x,qreal y,qreal size=26,QColor color=navy);
    void badge(QRectF rect,const QString& label,bool accent=false);
    void statusBadge(QRectF rect,OrderState state);
    void chip(QRectF rect,const QString& label,bool on=true);
    void emptyState(QRectF rect,const QString& icon,const QString& message);
    void progress(QRectF rect,double value,QColor color=navy);
    void dot(qreal x,qreal y,const QString& label,QColor color=green);
    void art(const QString& name,QRectF rect);
    void cover(QRectF rect);
    void detailRow(qreal y,const QString& key,const QString& value,QColor color=navy);
    bool showcase() const { return showcase_; }
private:
    void record(Hit hit);
    bool showcase_;
};
QString specs(const Order& order,bool compact=false);
QString money(qint64 cents);
QString capability(const Device& device);
int filterCount(const Snapshot& state,OrderFilter filter);
QVector<int> visibleOrders(const Snapshot& state,const Selection& selection);
// The backend has one global waiting order; per-device queues exist only in the showcase.
QVector<int> waitingOrders(const Snapshot& state);
void paintWorkbench(Painter& d,const Snapshot& state,const Selection& selection);
void paintQueue(Painter& d,const Snapshot& state,const Selection& selection);
void paintDevices(Painter& d,const Snapshot& state,const Selection& selection);
struct StatsMetricLayout {
    std::array<QRectF,4> columns;
    std::array<qreal,3> dividers;
    int valueSize = 32;
};
StatsMetricLayout statsMetricLayout(const Snapshot& state);
void paintStats(Painter& d,const Snapshot& state,const Selection& selection);
}
