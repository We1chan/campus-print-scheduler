#include "merchant_window.hpp"
#include "merchant_shell.hpp"
#include "merchant_dispatch_painter.hpp"
#include "editorial_chrome.hpp"
#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QShortcut>
#include <QMessageBox>
#include <QInputDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QMenu>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScrollArea>
#include <QLabel>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QSet>
#include <QSpinBox>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QRegion>
#include <QElapsedTimer>
#include <QAbstractItemView>
#include <QScreen>
#include <QStyleFactory>
#include <functional>
#include <algorithm>
static void initializeMerchantBrand() { Q_INIT_RESOURCE(merchant_assets); }
namespace cps::gui {
using namespace merchant;
QIcon merchantBrandIcon() {
    initializeMerchantBrand();QIcon icon;
    for(int size:{16,24,32,48,64,128,256})icon.addFile(QStringLiteral(":/merchant/icons-v3/brand-%1.png").arg(size),QSize(size,size));
    return icon;
}
namespace {
// The reference canvas is scaled uniformly, so text and photographs never
// stretch. Extra width centres the page between a full-width title bar and
// status bar; extra height goes below the page, above the status bar.
struct CanvasGeometry {
    qreal scale=1;                 // widget pixels per reference unit
    qreal width=canvasWidth;       // the whole widget, in reference units
    qreal height=canvasHeight;
    qreal offset=0;                // left edge of the centred page, in reference units
    static CanvasGeometry of(QSize size) {
        CanvasGeometry g;
        g.scale=std::max<qreal>(0.01,std::min(size.width()/canvasWidth,size.height()/canvasHeight));
        g.width=size.width()/g.scale;
        g.height=size.height()/g.scale;
        g.offset=(g.width-canvasWidth)/2;
        return g;
    }
    QTransform chrome() const {return QTransform::fromScale(scale,scale);}
    QTransform page() const {QTransform t=chrome();t.translate(offset,0);return t;}
    qreal shift() const {return width-canvasWidth;}  // for right-anchored chrome
    qreal status() const {return height-50;}          // top of the status bar
};
QRect pixels(const QTransform& transform,const QRectF& rect) {return transform.mapRect(rect).toAlignedRect();}
// Fill `target` with `image` at its own aspect ratio, cropping the bottom or right edge.
void drawCovering(QPainter& p,const QRectF& target,const QImage& image) {
    if(image.isNull()||target.isEmpty())return;
    const qreal aspect=target.width()/target.height();
    const qreal height=std::min<qreal>(image.height(),image.width()/aspect);
    p.drawImage(target,image,QRectF(0,0,height*aspect,height));
}
}
class MerchantButton : public QPushButton {
public:
    explicit MerchantButton(QWidget* parent):QPushButton(parent){setAttribute(Qt::WA_Hover);}
    void setHit(const Hit& hit) { hit_ = hit; }
protected:
    void paintEvent(QPaintEvent*) override {
        // Match student buttons: flat color/border feedback, transparent navigation and artwork.
        if(!hit_.button||(isEnabled()&&!underMouse()&&!isDown()&&!hasFocus()))return;
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(width()/hit_.rect.width(),height()/hit_.rect.height());
        const bool hovered=isEnabled()&&(underMouse()||isDown());
        const QColor accent("#D6402C");
        if(hit_.action.startsWith("dispatch-")){
            painter.setPen(QPen(accent,hasFocus()?2:1));
            painter.setBrush(hovered?QColor(hit_.primary?0:214,hit_.primary?0:61,hit_.primary?0:41,isDown()?35:18):QColor(Qt::transparent));
            painter.drawRoundedRect(QRectF(1,1,hit_.rect.width()-2,hit_.rect.height()-2),6,6);return;
        }
        QColor fill=hit_.primary?(hovered?QColor("#BF3322"):red):(hovered?QColor("#FFF6F2"):QColor(Qt::white));
        QColor border=hit_.primary?fill:accent;
        QColor ink=hit_.primary?QColor(Qt::white):navy;
        if(isDown()){fill=hit_.primary?QColor("#A72C1E"):QColor("#F7E9E3");border=hit_.primary?fill:accent;}
        if(!isEnabled()){fill=hit_.primary?QColor("#CFA097"):QColor("#F1F0ED");border=hit_.primary?fill:QColor("#E2E0DB");ink=hit_.primary?QColor("#FFF7F4"):QColor("#A2A7B0");}
        painter.setPen(QPen(border,isEnabled()&&hasFocus()?2:1));
        painter.setBrush(fill);
        const QRectF box(0,0,hit_.rect.width(),hit_.rect.height());
        painter.drawRoundedRect(box.adjusted(1,1,-1,-1),6,6);
        const int size=hit_.primary?21:16;
        const QFont font=merchantFont(size,hit_.primary);
        painter.setFont(font);painter.setPen(ink);
        painter.drawText(box.adjusted(12,0,-12,0),Qt::AlignCenter,QFontMetricsF(font).elidedText(hit_.label,Qt::ElideRight,box.width()-24));
    }
    void enterEvent(QEnterEvent* event) override {QPushButton::enterEvent(event);update();}
    void leaveEvent(QEvent* event) override {QPushButton::leaveEvent(event);update();}
    void mousePressEvent(QMouseEvent* event) override {dragOrigin_=event->position().toPoint();QPushButton::mousePressEvent(event);}
    void mouseMoveEvent(QMouseEvent* event) override {
        const auto id=property("dragOrderId").toString();
        if((event->buttons()&Qt::LeftButton)&&!id.isEmpty()&&(event->position().toPoint()-dragOrigin_).manhattanLength()>=QApplication::startDragDistance()){
            setDown(false);
            auto* drag=new QDrag(this);auto* mime=new QMimeData;
            mime->setData("application/x-paperroom-order",id.toUtf8());
            drag->setMimeData(mime);drag->setPixmap(grab());drag->setHotSpot(dragOrigin_);drag->exec(Qt::MoveAction);
            return;
        }
        QPushButton::mouseMoveEvent(event);
    }
private:
    QPoint dragOrigin_;
    Hit hit_;
};
class MerchantComboBox : public QComboBox {
public:
    explicit MerchantComboBox(QWidget* parent):QComboBox(parent){
        view()->window()->setWindowFlag(Qt::NoDropShadowWindowHint);
    }
protected:
    void paintEvent(QPaintEvent* event) override{
        QComboBox::paintEvent(event);QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(navy,1.4));const qreal x=width()-23,y=height()/2.;
        painter.drawLine(QPointF(x-5,y-2),QPointF(x,y+3));painter.drawLine(QPointF(x,y+3),QPointF(x+5,y-2));
    }
};
class MerchantCanvas : public QWidget {
public:
    MerchantCanvas(MerchantWindow* owner,MerchantData* data):QWidget(owner),data_(data) {
        if(data_->snapshot().showcase)selection.device=1;
        setObjectName("merchantCanvas");setMouseTracking(true);setAcceptDrops(true);setAttribute(Qt::WA_OpaquePaintEvent);
        search_=new QLineEdit(this);search_->setObjectName("merchantSearch");search_->setPlaceholderText(QStringLiteral("搜索订单 / 文件名"));
        search_->setAccessibleName(QStringLiteral("搜索订单、文件名或学号"));
        // Font size follows the canvas scale in refresh(), so it is not fixed here.
        search_->setStyleSheet("QLineEdit{border:none;background:transparent;color:#14213E;selection-background-color:#F4D9D1;selection-color:#14213E;}QLineEdit:focus{border-bottom:1px solid #D6402C;}");
        QObject::connect(search_,&QLineEdit::textChanged,this,[this](const QString& value){selection.search=value.trimmed();selection.offset=0;refresh();});
        QObject::connect(data_,&MerchantData::changed,this,[this]{refresh();});
        demo=makeDispatchDemo(4);
        const QString comboStyle="QComboBox{background:white;border:1px solid #DADDE2;border-radius:6px;color:#14213E;padding:7px 14px;}QComboBox:hover,QComboBox:focus{border:1px solid #D63D29;}QComboBox::drop-down{border:none;width:30px;}QComboBox QAbstractItemView{background:#FAFAF9;color:#14213E;selection-background-color:#FFF6F2;selection-color:#D63D29;border:1px solid #DADDE2;padding:6px;}";
        scenario_=new MerchantComboBox(this);scenario_->setObjectName("merchantDispatchScenario");scenario_->setAccessibleName(QStringLiteral("调度演示场景"));
        scenario_->setStyleSheet(comboStyle);scenario_->addItems(dispatchScenarioNames());scenario_->setCurrentIndex(4);
        speed_=new MerchantComboBox(this);speed_->setObjectName("merchantDispatchSpeed");speed_->setAccessibleName(QStringLiteral("演示播放速度"));
        speed_->setStyleSheet(comboStyle);speed_->addItems({"1×","2×","4×"});
        QObject::connect(scenario_,&QComboBox::currentIndexChanged,this,[this](int index){
            demo=makeDispatchDemo(index);demoSelection.nowMs=0;demoSelection.order.clear();demoSelection.orderOffset=0;pauseDispatch();refresh();
        });
        demoTimer_.setParent(this);demoTimer_.setInterval(50);
        QObject::connect(&demoTimer_,&QTimer::timeout,this,[this]{
            const auto elapsed=static_cast<cps::TimeMs>(demoClock_.restart());
            demoSelection.nowMs=std::min(demo.horizonMs,demoSelection.nowMs+elapsed*20*(cps::TimeMs{1}<<speed_->currentIndex()));
            if(demoSelection.nowMs>=demo.horizonMs)pauseDispatch();
            refresh();
        });
        const EditorialWindowButton::Action actions[]{EditorialWindowButton::Minimize,EditorialWindowButton::Maximize,EditorialWindowButton::Close};
        const QStringList names{"windowMinimize","windowMaximize","windowClose"};
        for(int i=0;i<3;++i){
            auto* control=new EditorialWindowButton(owner,actions[i],names[i]);
            control->setParent(this);control->setFocusPolicy(Qt::TabFocus);windowControls_<<control;
        }
    }
    int page=0;Selection selection;
    DispatchDemo demo;DispatchDemoSelection demoSelection;
    std::function<void(const QString&,const QString&)> invoked;
    QString toast;bool toastProblem=false;
    qreal titleBand() const {return 56*CanvasGeometry::of(size()).scale;}
    void pauseDispatch(){demoTimer_.stop();demoSelection.playing=false;}
    void clearSearch(){search_->clear();}
    void dispatchAction(const QString& action,const QString& value){
        if(action=="dispatch-policy")demoSelection.policy=std::clamp(value.toInt(),0,2);
        else if(action=="dispatch-order"){
            demoSelection.order=demoSelection.order==value?QString():value;
            for(int i=0;i<demo.orders.size();++i)if(demo.orders[i].id==value){demoSelection.orderOffset=i/6*6;break;}
        }else if(action=="dispatch-page"){
            const int pages=std::max(1,(int(demo.orders.size())+5)/6);
            demoSelection.orderOffset=std::clamp(demoSelection.orderOffset/6+value.toInt(),0,pages-1)*6;
        }else if(action=="dispatch-reload"){
            pauseDispatch();demo=makeDispatchDemo(scenario_->currentIndex());
            demoSelection.nowMs=0;demoSelection.order.clear();demoSelection.orderOffset=0;
        }else if(action=="dispatch-reset"){pauseDispatch();demoSelection.nowMs=0;demoSelection.order.clear();demoSelection.orderOffset=0;}
        else if(action=="dispatch-play"){
            if(demoSelection.playing)pauseDispatch();
            else{if(demoSelection.nowMs>=demo.horizonMs)demoSelection.nowMs=0;demoSelection.playing=true;demoClock_.start();demoTimer_.start();}
        }else if(action=="dispatch-step"){
            pauseDispatch();cps::TimeMs next=demo.horizonMs;
            for(const auto& dispatch:demo.runs[static_cast<std::size_t>(demoSelection.policy)].result.dispatches){
                if(dispatch.startedMs>demoSelection.nowMs)next=std::min(next,dispatch.startedMs);
                if(dispatch.finishedMs>demoSelection.nowMs)next=std::min(next,dispatch.finishedMs);
            }
            demoSelection.nowMs=next;
        }
        refresh();
    }
    void refresh() {
        const auto& state=data_->snapshot();
        if(selection.order>=state.orders.size())selection.order=std::max(0,int(state.orders.size())-1);
        if(selection.device>=state.devices.size())selection.device=std::max(0,int(state.devices.size())-1);
        const auto g=CanvasGeometry::of(size());
        QImage scratch(1,1,QImage::Format_RGB32);QPainter painter(&scratch);QVector<Hit> controls;paintScene(painter,controls,g);painter.end();
        syncButtons(controls,g);
        search_->setGeometry(pixels(g.page(),{826,317,180,33}));search_->setFont(scaledFont(14,g));
        search_->setVisible(page==0);search_->raise();
        scenario_->setGeometry(pixels(g.page(),{92,231,245,47}));speed_->setGeometry(pixels(g.page(),{763,231,127,47}));
        scenario_->setFont(scaledFont(16,g));speed_->setFont(scaledFont(16,g));
        scenario_->setVisible(page==4);speed_->setVisible(page==4);scenario_->raise();speed_->raise();
        setProperty("dispatchTimeMs",QVariant::fromValue<qlonglong>(demoSelection.nowMs));setProperty("dispatchPolicy",demoSelection.policy);
        setProperty("dispatchPlaying",demoSelection.playing);setProperty("dispatchOrderOffset",demoSelection.orderOffset);
        setProperty("dispatchOrderCount",demo.orders.size());setProperty("dispatchProvenOptimal",demo.runs[2].provenOptimal);
        for(int i=0;i<windowControls_.size();++i){
            windowControls_[i]->setGeometry(pixels(g.chrome(),{1447+i*46+g.shift(),3,43,49}));
            windowControls_[i]->show();windowControls_[i]->raise();
        }
        update();
    }
    void focusSearch() {pauseDispatch();page=0;refresh();search_->setFocus();}
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {if(acceptsOrderDrop(event->mimeData()))event->acceptProposedAction();}
    void dragMoveEvent(QDragMoveEvent* event) override {if(acceptsOrderDrop(event->mimeData()))event->acceptProposedAction();}
    void dropEvent(QDropEvent* event) override {
        if(!acceptsOrderDrop(event->mimeData()))return;
        const auto source=QString::fromUtf8(event->mimeData()->data("application/x-paperroom-order"));
        for(auto* button:std::as_const(buttons_)){
            const auto target=button->property("dropOrderId").toString();
            if(target.isEmpty()||!button->geometry().contains(event->position().toPoint()))continue;
            data_->execute("move-before",source+":"+target);event->acceptProposedAction();return;
        }
    }
    void wheelEvent(QWheelEvent* event) override {
        const int step=event->angleDelta().y()<0?1:-1;
        if(page==0){
            const int maximum=std::max(0,int(visibleOrders(data_->snapshot(),selection).size())-5);
            selection.offset=std::clamp(selection.offset+step,0,maximum);
        }else if(page==1)selection.queueOffset=std::max(0,selection.queueOffset+step);
        else if(page==4&&CanvasGeometry::of(size()).page().inverted().map(event->position()).x()<304)dispatchAction("dispatch-page",QString::number(step));
        refresh();event->accept();
    }
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);refresh();}
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QVector<Hit> controls;paintScene(painter,controls,CanvasGeometry::of(size()));
    }
private:
    static QFont scaledFont(int size,const CanvasGeometry& g) {
        QFont font(QStringLiteral("Microsoft YaHei UI"));font.setPixelSize(std::max(9,qRound(size*g.scale)));return font;
    }
    bool acceptsOrderDrop(const QMimeData* mime) const {
        return page==1&&data_->snapshot().showcase&&mime->hasFormat("application/x-paperroom-order");
    }
    // Buttons are keyed by what they do, not where they are, so a resize moves
    // the same widgets (keeping focus and hover) instead of recreating them.
    void syncButtons(const QVector<Hit>& controls,const CanvasGeometry& g) {
        const auto& state=data_->snapshot();
        QSet<QString> active;QHash<QString,int> seen;
        for(auto hit:controls) {
            if(hit.action=="search")continue;
            // Some painters record hits directly; those are always page hits.
            if(hit.bounds.isNull())hit.bounds=g.page().mapRect(hit.rect);
            const QString base=hit.action+"\n"+hit.value+"\n"+hit.label;
            const QString key=base+"\n"+QString::number(seen[base]++);
            active.insert(key);
            auto* button=buttons_.value(key,nullptr);
            if(!button) {
                button=new MerchantButton(this);buttons_.insert(key,button);
                button->setObjectName("merchant_"+hit.action+"_"+hit.value);
                button->setAccessibleName(hit.label);button->setFocusPolicy(Qt::StrongFocus);
                const QString action=hit.action,value=hit.value;
                QObject::connect(button,&QPushButton::clicked,this,[this,action,value]{if(invoked)invoked(action,value);});
                if(action=="select-order") {
                    button->setContextMenuPolicy(Qt::CustomContextMenu);
                    QObject::connect(button,&QWidget::customContextMenuRequested,this,[this,value]{if(invoked)invoked("order-menu",value);});
                }
            }
            static_cast<MerchantButton*>(button)->setHit(hit);
            button->setGeometry(hit.bounds.toAlignedRect());button->show();
            const int row=hit.value.toInt();const bool orderHit=hit.action=="select-order"&&row>=0&&row<state.orders.size();
            button->setProperty("dropOrderId",orderHit?state.orders[row].id:QString());
            button->setProperty("dragOrderId",orderHit&&page==1&&state.showcase&&state.orders[row].state==OrderState::Waiting?state.orders[row].id:QString());
            const bool unsupported=!state.showcase&&requiresShowcase(hit.action);
            button->setEnabled(hit.enabled&&!unsupported);
            button->setCursor(button->isEnabled()?Qt::PointingHandCursor:Qt::ArrowCursor);
            button->setToolTip(unsupported?QStringLiteral("共享后台尚未提供该手动操作；可在店铺菜单选择自动调度策略。"):hit.label);
        }
        for(auto i=buttons_.begin();i!=buttons_.end();) {
            if(!active.contains(i.key())) {delete i.value();i=buttons_.erase(i);}
            else ++i;
        }
    }
    void paintBackground(QPainter& p,const CanvasGeometry& g) {
        p.setTransform(g.chrome());
        p.fillRect(QRectF(0,0,g.width,g.height),Qt::white);
        p.save();p.setOpacity(.18);p.drawTiledPixmap(QRectF(0,0,g.width,g.height),QPixmap::fromImage(assets_.texture));p.restore();
        if(page==4) {
            p.fillRect(QRectF(0,56,g.width,g.status()-56),QColor("#FDFEFE"));
            if(assets_.dispatchBackground.isNull())p.fillRect(QRectF(0,56,g.width,162),QColor("#F7F6F2"));
            else drawCovering(p,QRectF(0,56,g.width,162),assets_.dispatchBackground);
        } else drawCovering(p,QRectF(0,56,g.offset+1134,heroBottom-56),assets_.paperBackground);
    }
    void paintPage(Painter& d,const CanvasGeometry& g) {
        const auto& state=data_->snapshot();
        d.p.setTransform(g.page());
        if(page==4){paintDispatch(d,demo,demoSelection);return;}
        d.p.setPen(QPen(ruleColor,1));d.p.drawLine(QPointF(1145,65),QPointF(1145,page==3 ? 852 : 930));
        if(page==0)paintWorkbench(d,state,selection);
        else if(page==1)paintQueue(d,state,selection);
        else if(page==2)paintDevices(d,state,selection);
        else paintStats(d,state,selection);
    }
    void paintTitleBar(Painter& d,const CanvasGeometry& g) {
        auto& p=d.p;
        p.setTransform(g.chrome());
        p.fillRect(QRectF(0,0,g.width,56),QColor("#FAFAF9"));d.rule(0,55,g.width);
        p.drawImage(QRectF(39,10,41,35),QImage(":/merchant/icons-v3/brand-128.png"));
        d.text(92,8,63,38,QStringLiteral("纸间"),24,navy,true);p.setPen(ruleColor);p.drawLine(163,16,163,40);
        d.text(181,8,92,38,QStringLiteral("店主端"),16,slate);
        const qreal centre=g.shift()/2;
        if(page==4){
            d.text(690+centre,5,205,45,QStringLiteral("调度工作台"),18,navy,true,false,Qt::AlignCenter);p.fillRect(QRectF(735+centre,52,115,4),red);
            d.button({1187+g.shift(),10,168,39},QStringLiteral("‹  返回打印队列"),"dispatch-back");
            return;
        }
        const QStringList nav{QStringLiteral("订单工作台"),QStringLiteral("打印队列"),QStringLiteral("设备管理"),QStringLiteral("营业统计")};
        const int positions[4]={562,706,826,947};
        for(int i=0;i<4;++i) {
            const QRectF tab(positions[i]-6+centre,0,i==0?114:106,56);
            d.navigationText(tab,nav[i]);
            d.hit(tab,"nav",QString::number(i),nav[i]);
            if(i==page){const qreal lineWidth=i==0?100:88;p.fillRect(QRectF(tab.center().x()-lineWidth/2,52,lineWidth,4),red);}
        }
        if(data_->snapshot().demoAvailable){
            d.button({1080+g.shift(),11,128,34},QStringLiteral("演示 +50 单"),"demo-add-50");
            d.button({1220+g.shift(),11,108,34},QStringLiteral("清空订单"),"demo-clear-orders");
            d.icon("shop",1347+g.shift(),18,23);d.text(1380+g.shift(),3,45,50,QStringLiteral("店铺"),16);
            d.hit({1338+g.shift(),7,99,42},"shop-menu",{},QStringLiteral("店铺菜单"));
        }else{
            d.icon("shop",1240+g.shift(),18,23);d.text(1275+g.shift(),3,70,50,QStringLiteral("校园一店"),16);
            d.hit({1230+g.shift(),7,120,42},"shop-menu",{},QStringLiteral("店铺菜单"));
        }
    }
    void paintStatusBar(Painter& d,const CanvasGeometry& g) {
        auto& p=d.p;const auto& state=data_->snapshot();
        p.setTransform(g.chrome());
        const qreal y=g.status();
        p.fillRect(QRectF(0,y,g.width,50),Qt::white);
        if(page==4){
            d.rule(0,y-1,g.width);
            p.setPen(Qt::NoPen);p.setBrush(green);p.drawEllipse(QPointF(42,y+23),4,4);p.setBrush(Qt::NoBrush);
            d.text(70,y+5,395,35,QStringLiteral("独立演示 · 不影响真实队列"),13);
            d.text(g.width-526,y+5,495,35,demo.stress?QStringLiteral("离线 100 单 · 原始毫秒时间轴 · 整批结果"):QStringLiteral("本地仿真 · 教学时钟 3 页/分 · 整批结果"),
                   13,slate,false,false,Qt::AlignRight|Qt::AlignVCenter);
            return;
        }
        d.rule(39,y,g.width-79);
        const bool open=state.connected||state.showcase;
        p.setPen(Qt::NoPen);p.setBrush(open?green:slate);p.drawEllipse(QPointF(44,y+25),3,3);p.setBrush(Qt::NoBrush);
        d.text(63,y+1,100,48,open?QStringLiteral("店铺营业中"):QStringLiteral("连接后台中"),13,navy);
        const auto online=std::count_if(state.devices.cbegin(),state.devices.cend(),[](const Device& device){return device.online;});
        d.text(175,y+1,151,48,"06:00 - 23:00",13,slate);
        d.text(344,y+1,196,48,QStringLiteral("%1 台设备在线").arg(online),13,slate);
        if(state.showcase)d.text(556,y+1,215,48,QStringLiteral("样例模式 · 独立演示"),13,slate);
        else if(state.paused)d.text(556,y+1,215,48,QStringLiteral("派发已暂停"),13,red);
        else if(state.demoAvailable)d.text(556,y+1,215,48,QStringLiteral("演示模式 · 新单半价"),13,slate);
        d.text(g.width-255,y+1,221,48,state.date+" "+state.clock,13,slate,false,false,Qt::AlignRight|Qt::AlignVCenter);
    }
    void paintScene(QPainter& p,QVector<Hit>& controls,const CanvasGeometry& g) {
        Painter d(p,controls,assets_,data_->snapshot().showcase);
        paintBackground(p,g);
        paintPage(d,g);
        paintTitleBar(d,g);
        paintStatusBar(d,g);
        if(toast.isEmpty())return;
        p.setTransform(g.page());
        d.panel({480,795,625,51},toastProblem?QColor("#FFF2EE"):QColor("#F5F7FA"),toastProblem?red:ruleColor,8);
        d.text(499,797,590,47,toast,16,toastProblem?red:navy);
    }
    MerchantData* data_;Assets assets_;QHash<QString,QPushButton*> buttons_;QLineEdit* search_;QVector<EditorialWindowButton*> windowControls_;
    QComboBox* scenario_;QComboBox* speed_;QTimer demoTimer_;QElapsedTimer demoClock_;
};
class MerchantMenu : public QMenu {
public:
    explicit MerchantMenu(QWidget* parent):QMenu(parent) {
        setWindowFlags(windowFlags()|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        // Vista menu items themselves contain the native popup shadow. Use a
        // flat base style only for these menus, retaining our shared stylesheet.
        auto* flatStyle=QStyleFactory::create(QStringLiteral("Fusion"));
        flatStyle->setParent(this);setStyle(flatStyle);
    }
protected:
    void showEvent(QShowEvent* event) override {
        QMenu::showEvent(event);
        auto* parentMenu=qobject_cast<QMenu*>(parentWidget());
        if(!parentMenu||!parentMenu->isVisible())return;
        const auto row=parentMenu->actionGeometry(menuAction());
        if(row.isEmpty())return;
        // Align the paper edges, not the outer transparent shadow margins.
        const auto available=screen()->availableGeometry();
        int x=parentMenu->x()+parentMenu->width()-32;
        if(x+width()>available.right()+1)x=parentMenu->x()-width()+32;
        const int y=std::clamp(parentMenu->y()+row.top()-22,available.top(),std::max(available.top(),available.bottom()-height()+1));
        move(x,y);
    }
    void resizeEvent(QResizeEvent* event) override {
        QMenu::resizeEvent(event);
        // Clip only the transparent window's outer corners. The paper panel
        // and its rounded halo are inset, so neither gets a rectangular tail.
        QPainterPath outline;outline.addRoundedRect(QRectF(rect()),12,12);
        setMask(QRegion(outline.toFillPolygon().toPolygon()));
    }
    void paintEvent(QPaintEvent* event) override {
        {
            QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(rect(),Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.setPen(Qt::NoPen);
            const QRectF paper=QRectF(rect()).adjusted(16,16,-16,-16);
            // Keep the halo as quiet as the main window's compositor shadow.
            for(int spread=10;spread>=1;--spread){
                painter.setBrush(QColor(0,0,0,2));
                painter.drawRoundedRect(paper.adjusted(-spread,-spread,spread,spread).translated(0,2),8+spread,8+spread);
            }
        }
        QMenu::paintEvent(event);
    }
};
static void styleMerchantMenu(QMenu& menu) {
    menu.setStyleSheet("QMenu{background:#FAFAF9;color:#14213E;border:1px solid #DADDE2;border-radius:8px;margin:16px;padding:6px;font-family:'Microsoft YaHei UI';font-size:14px;}QMenu::item{padding:9px 24px 9px 15px;border-radius:5px;}QMenu::item:selected{background:#FFF6F2;color:#D63D29;}QMenu::separator{height:1px;background:#DADDE2;margin:6px 10px;}");
}
static EditorialDialog* formDialog(QWidget* parent,const QString& title,const QString& subtitle,QFormLayout*& form) {
    auto* dialog=new EditorialDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* root=new QVBoxLayout(dialog);root->setContentsMargins(0,0,0,0);
    root->addWidget(editorialDialogHeader(dialog,title,subtitle));
    form=new QFormLayout;form->setContentsMargins(24,20,24,20);root->addLayout(form);
    dialog->setWindowModality(Qt::WindowModal);
    return dialog;
}
static QDialogButtonBox* addDialogButtons(EditorialDialog* dialog) {
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
    static_cast<QVBoxLayout*>(dialog->layout())->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    return buttons;
}
MerchantWindow::MerchantWindow(WindowOptions options,bool showcase):model_(new MerchantData(options,showcase,this)),canvas_(new MerchantCanvas(this,model_)) {
    setFont(QFont(QStringLiteral("Microsoft YaHei UI"),10));
    setObjectName("merchantWindow");setWindowTitle(QStringLiteral("纸间 · 店主端"));setCentralWidget(canvas_);
    setMinimumSize(950,594);resize(1269,794);
    auto* chrome=new EditorialChrome(this,56,992);
    chrome->setTitleBand([canvas=canvas_]{return canvas->titleBand();});
    setWindowIcon(merchantBrandIcon());
    configureMerchantTaskbar(this);
    canvas_->invoked=[this](const QString& a,const QString& v){action(a,v);};
    connect(model_,&MerchantData::feedback,this,&MerchantWindow::notify);connect(model_,&MerchantData::backendReady,this,&MerchantWindow::backendReady);
    for(int i=0;i<4;++i){auto* key=new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i+1)),this);connect(key,&QShortcut::activated,this,[this,i]{setPage(i);});}
    auto* find=new QShortcut(QKeySequence::Find,this);connect(find,&QShortcut::activated,this,[this]{canvas_->focusSearch();});
    auto* refresh=new QShortcut(QKeySequence("Ctrl+R"),this);connect(refresh,&QShortcut::activated,this,[this]{model_->execute("refresh");});
    QTimer::singleShot(0,model_,&MerchantData::start);
}
void MerchantWindow::setPage(int index) {
    if(canvas_->page<4)deviceSelections_[canvas_->page]=canvas_->selection.device;
    canvas_->pauseDispatch();
    canvas_->page=std::clamp(index,0,3);canvas_->selection.device=deviceSelections_[canvas_->page];canvas_->refresh();
}
void MerchantWindow::openDispatchWorkspace(){if(canvas_->page<4)deviceSelections_[canvas_->page]=canvas_->selection.device;canvas_->page=4;canvas_->refresh();}
void MerchantWindow::notify(const QString& value,bool problem) {
    canvas_->toast=value;canvas_->toastProblem=problem;canvas_->setAccessibleDescription(value);canvas_->update();
    QTimer::singleShot(4800,canvas_,[this,value]{
        if(canvas_->toast!=value)return;
        canvas_->toast.clear();canvas_->setAccessibleDescription({});canvas_->update();
    });
}
void MerchantWindow::captureAll(const QString& directory) {
    QDir().mkpath(directory);resize(qRound(1586/devicePixelRatioF()),qRound(992/devicePixelRatioF()));
    const QStringList names{"workbench","queue","devices","stats"};
    const auto save=[this,&directory](const QString& name){
        QApplication::processEvents();if(auto* focus=QApplication::focusWidget())focus->clearFocus();
        canvas_->grab().toImage().scaled(1586,992,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(QDir(directory).filePath(name+".png"));
    };
    for(int i=0;i<4;++i){setPage(i);save(names[i]);}
    openDispatchWorkspace();save("dispatch");setPage(0);
}
void MerchantWindow::viewFile() {
    const auto& state=model_->snapshot();if(state.orders.isEmpty())return;const auto& order=state.orders[canvas_->selection.order];
    if(!order.path.isEmpty()&&QFileInfo::exists(order.path)){QDesktopServices::openUrl(QUrl::fromLocalFile(order.path));return;}
    auto* dialog=new EditorialDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(0,0,0,0);
    layout->addWidget(editorialDialogHeader(dialog,QStringLiteral("文件预览"),order.title));
    auto* note=new QLabel(QStringLiteral("订单首页由学生网页上传后共享；历史订单可选择封面图片补充，打印参数保持不变。"));
    note->setWordWrap(true);note->setContentsMargins(20,10,20,10);layout->addWidget(note);
    auto* paper=new QLabel;paper->setAlignment(Qt::AlignCenter);paper->setMinimumSize(420,470);layout->addWidget(paper);
    Assets art;const QImage cover=art.orderCover(order.coverPath);
    if(!cover.isNull())paper->setPixmap(QPixmap::fromImage(cover.scaled(420,470,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
    else paper->setText(QStringLiteral("暂无文件封面"));
    const QString id=order.id;
    auto* pick=new QPushButton(QStringLiteral("选择本机封面图片"));pick->setObjectName("merchantChooseCover");
    connect(pick,&QPushButton::clicked,dialog,[this,id,paper,note]{
        const auto file=QFileDialog::getOpenFileName(this,QStringLiteral("选择订单封面图片"),{},QStringLiteral("图片 (*.png *.jpg *.jpeg)"));
        if(file.isEmpty())return;
        Assets art;const QImage image=art.orderCover(file);
        if(image.isNull()){note->setText(QStringLiteral("图片无法读取或尺寸超过限制。"));return;}
        if(!model_->setOrderCover(id,image)){note->setText(QStringLiteral("封面未能保存，请检查店铺目录。"));return;}
        paper->setPixmap(QPixmap::fromImage(image.scaled(420,470,Qt::KeepAspectRatio,Qt::SmoothTransformation)));
        note->setText(QStringLiteral("已保存封面图片。"));
    });
    layout->addWidget(pick);dialog->resize(550,650);dialog->setWindowModality(Qt::WindowModal);dialog->open();
}
void MerchantWindow::action(const QString& name,const QString& value) {
    if(name.startsWith("dispatch-")) {
        if(name=="dispatch-open")openDispatchWorkspace();
        else if(name=="dispatch-back")setPage(1);
        else canvas_->dispatchAction(name,value);
        return;
    }
    auto& selection=canvas_->selection;
    const auto refreshed=[this](auto change){return [this,change]{change();canvas_->refresh();};};
    const QHash<QString,std::function<void()>> handlers{
        {"nav",[&]{setPage(value.toInt());}},
        {"page",[&]{if(value=="devices")setPage(2);}},
        {"order-details",[&]{setPage(0);}},
        {"select-order",refreshed([&]{selection.order=value.toInt();})},
        {"select-device",[&]{selectDevice(value.toInt());}},
        {"filter",[&]{
            canvas_->clearSearch();
            selection.filter=static_cast<OrderFilter>(value.toInt());selection.offset=0;canvas_->refresh();
        }},
        {"queue-device-page",refreshed([&]{selection.device=value.toInt();selection.queueOffset=0;})},
        {"table-prev",refreshed([&]{selection.offset=std::max(0,value.toInt());})},
        {"table-next",refreshed([&]{selection.offset=std::max(0,value.toInt());})},
        {"queue-prev",refreshed([&]{selection.queueOffset=std::max(0,value.toInt());})},
        {"queue-next",refreshed([&]{selection.queueOffset=std::max(0,value.toInt());})},
        {"device-prev",[&]{pageDevices(false);}},
        {"device-next",[&]{pageDevices(true);}},
        {"view-file",[&]{viewFile();}},
        {"range",[&]{selectRange(value);}},
        {"export-report",[&]{exportReport();}},
        {"order-menu",[&]{showOrderMenu(value.toInt());}},
        {"shop-menu",[&]{showShopMenu();}},
        {"device-settings",[&]{openDeviceSettings(false,value);}},
        {"add-device",[&]{openDeviceSettings(model_->snapshot().showcase,value);}},
        {"restock",[&]{openRestock();}},
        {"assign",[&]{chooseSampleDevice(name,value);}},
        {"transfer",[&]{chooseSampleDevice(name,value);}},
        {"reorder",[&]{chooseSampleDevice(name,value);}},
    };
    if(const auto handler=handlers.constFind(name);handler!=handlers.cend())(*handler)();
    else model_->execute(name,value);
}
void MerchantWindow::selectDevice(int index) {
    const auto& state=model_->snapshot();
    // A live workbench card opens that device's page: assignment is automatic there.
    if(!state.showcase&&canvas_->page==0){deviceSelections_[2]=index;setPage(2);return;}
    canvas_->selection.device=index;
    // On the live queue page a busy device shows the order it is printing.
    if(!state.showcase&&canvas_->page==1&&index>=0&&index<state.devices.size()&&state.devices[index].busy)
        for(int i=0;i<state.orders.size();++i)if(state.orders[i].id==state.devices[index].jobId)canvas_->selection.order=i;
    canvas_->refresh();
}
void MerchantWindow::pageDevices(bool next) {
    const auto& state=model_->snapshot();
    qsizetype current=0;
    for(qsizetype i=0;i<state.devices.size();++i)if(state.devices[i].index==canvas_->selection.device){current=i;break;}
    const qsizetype target=(current/2)*2+(next?2:-2);
    if(target<0||target>=state.devices.size())return;
    canvas_->selection.device=state.devices[target].index;canvas_->refresh();
}
void MerchantWindow::selectRange(const QString& label) {
    QString range=label==QStringLiteral("今天")?"today":label==QStringLiteral("本周")?"week":label==QStringLiteral("本月")?"month":label;
    if(range==QStringLiteral("自定义")) {
        const QString date=QInputDialog::getText(this,QStringLiteral("统计日期"),QStringLiteral("输入日期（yyyy-MM-dd）"));
        if(date.isEmpty())return;
        if(!QDate::fromString(date,Qt::ISODate).isValid()){notify(QStringLiteral("请输入有效的 yyyy-MM-dd 日期"),true);return;}
        range=date;
    }
    canvas_->selection.range=label;model_->setRange(range);canvas_->refresh();
}
void MerchantWindow::exportReport() {
    const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出营业报表"),"营业报表.json","JSON (*.json)");
    if(!path.isEmpty())model_->exportReport(path);
}
void MerchantWindow::showOrderMenu(int index) {
    const auto& state=model_->snapshot();
    if(index<0||index>=state.orders.size())return;
    canvas_->selection.order=index;canvas_->refresh();
    const auto order=state.orders[index];
    MerchantMenu menu(this);menu.setObjectName("merchantOrderMenu");styleMerchantMenu(menu);
    menu.addAction(QStringLiteral("查看文件"),this,&MerchantWindow::viewFile);
    if(order.state==OrderState::Waiting)menu.addAction(QStringLiteral("取消订单"),this,[this,order]{model_->execute("cancel",order.id);});
    if(order.state==OrderState::Ready&&state.showcase)menu.addAction(QStringLiteral("确认取件"),this,[this,order]{model_->execute("pickup",order.id);});
    menu.exec(QCursor::pos());
}
void MerchantWindow::showShopMenu() {
    const auto& state=model_->snapshot();
    MerchantMenu menu(this);menu.setObjectName("merchantShopMenu");styleMerchantMenu(menu);
    menu.addAction(QStringLiteral("订单工作台"),this,[this]{setPage(0);});
    menu.addAction(QStringLiteral("刷新后台"),this,[this]{model_->execute("refresh");});
    if(!state.showcase){
        menu.addSeparator();
        auto* policy=new MerchantMenu(&menu);policy->setTitle(QStringLiteral("自动调度策略"));policy->setObjectName("merchantPolicyMenu");
        styleMerchantMenu(*policy);menu.addMenu(policy);
        const QStringList values{"fcfs","sjf","balanced"};
        for(const auto& value:values) {
            auto* item=policy->addAction(policyLabel(value),this,[this,value]{model_->execute("policy",value);});
            item->setCheckable(true);item->setChecked(policyLabel(state.policy)==policyLabel(value));
        }
        menu.addAction(state.paused?QStringLiteral("恢复全部派发"):QStringLiteral("暂停全部派发"),this,[this]{model_->execute(model_->snapshot().paused?"resume-all":"pause-all");});
    }
    auto* trigger=canvas_->findChild<QPushButton*>("merchant_shop-menu_");
    menu.exec(trigger?trigger->mapToGlobal(QPoint(-16,trigger->height()-12)):QCursor::pos());
}
void MerchantWindow::openDeviceSettings(bool adding,const QString& value) {
    const auto& state=model_->snapshot();
    if(adding&&state.devices.size()>=6){notify(QStringLiteral("样例最多支持六台设备。"),true);return;}
    int index=value.isEmpty()?canvas_->selection.device:value.toInt();
    if(!adding&&!state.showcase&&value.isEmpty()) {
        // "Add device" on the live backend reconfigures one of its six fixed slots.
        QStringList deviceSlots;
        for(const auto& d:state.devices)deviceSlots<<d.name+(d.online?QStringLiteral("（接单中）"):QStringLiteral("（停止接单）"));
        bool ok=false;
        const auto chosen=QInputDialog::getItem(this,QStringLiteral("配置设备槽位"),QStringLiteral("共享后台提供六个固定槽位，请选择要启用或重新配置的设备。"),deviceSlots,0,false,&ok);
        if(!ok)return;
        index=static_cast<int>(deviceSlots.indexOf(chosen));
    }
    if(!adding&&(index<0||index>=state.devices.size()))return;
    const auto device=adding?Device{}:state.devices[index];
    QFormLayout* form=nullptr;
    auto* dialog=formDialog(this,QStringLiteral("设备设置"),device.name,form);
    auto* deviceName=new QLineEdit(adding?QStringLiteral("新打印机 %1").arg(state.devices.size()+1):device.name);
    auto* color=new QCheckBox(QStringLiteral("支持彩色"));color->setChecked(device.color);
    auto* duplex=new QCheckBox(QStringLiteral("支持双面"));duplex->setChecked(device.duplex);
    auto* a3=new QCheckBox(QStringLiteral("支持 A3"));a3->setChecked(device.a3);
    auto* enabled=new QCheckBox(QStringLiteral("自动接单"));enabled->setChecked(device.enabled);
    form->addRow(QStringLiteral("设备名称"),deviceName);form->addRow(QStringLiteral("色彩"),color);
    form->addRow(QStringLiteral("打印方式"),duplex);form->addRow(QStringLiteral("纸张"),a3);form->addRow(QStringLiteral("接单状态"),enabled);
    connect(addDialogButtons(dialog),&QDialogButtonBox::accepted,dialog,[this,dialog,index,adding,deviceName,color,duplex,a3,enabled]{
        if(deviceName->text().trimmed().isEmpty()){deviceName->setFocus();return;}
        int target=index;
        if(adding){target=static_cast<int>(model_->snapshot().devices.size());model_->addSampleDevice(deviceName->text(),color->isChecked());}
        model_->setDevice(target,{{"name",deviceName->text().trimmed()},{"color",color->isChecked()},{"duplex",duplex->isChecked()},{"a3",a3->isChecked()},{"enabled",enabled->isChecked()}});
        dialog->accept();
    });
    dialog->resize(500,380);dialog->open();
}
void MerchantWindow::openRestock() {
    const auto& state=model_->snapshot();
    const QString subtitle=state.showcase?QStringLiteral("样例补纸将同步增加各设备的纸张余量")
        :QStringLiteral("增加店铺共享纸库；当前 A4 %1 张 · A3 %2 张").arg(state.stockA4).arg(state.stockA3);
    QFormLayout* form=nullptr;
    auto* dialog=formDialog(this,QStringLiteral("补充纸张"),subtitle,form);
    auto* paper=new QComboBox;paper->addItems({"A4","A3"});
    auto* quantity=new QSpinBox;quantity->setRange(1,1000000);quantity->setValue(500);quantity->setSuffix(QStringLiteral(" 张"));
    form->addRow(QStringLiteral("纸张规格"),paper);form->addRow(QStringLiteral("补充数量"),quantity);
    connect(addDialogButtons(dialog),&QDialogButtonBox::accepted,dialog,[this,dialog,paper,quantity]{
        model_->execute("restock",paper->currentText().toLower()+":"+QString::number(quantity->value()));dialog->accept();
    });
    dialog->resize(530,260);dialog->open();
}
// Manual placement exists only in the showcase; the live backend dispatches by policy.
void MerchantWindow::chooseSampleDevice(const QString& name,const QString& value) {
    const auto& state=model_->snapshot();
    const int orderIndex=value.isEmpty()?canvas_->selection.order:value.toInt();
    if(orderIndex<0||orderIndex>=state.orders.size())return;
    const auto& o=state.orders[orderIndex];
    bool ok=false;
    if(name=="reorder"){
        const QStringList list{QStringLiteral("向前调整一位"),QStringLiteral("向后调整一位")};
        const auto chosen=QInputDialog::getItem(this,QStringLiteral("调整顺序"),o.title,list,0,false,&ok);
        if(ok)model_->execute(name,o.id+":"+(chosen==list[0]?"up":"down"));
        return;
    }
    QStringList names;for(const auto& d:state.devices)names<<d.name;
    const auto chosen=QInputDialog::getItem(this,QStringLiteral("分配打印设备"),o.title,names,std::max(0,canvas_->selection.device),false,&ok);
    if(ok)model_->execute(name,o.id+":"+QString::number(names.indexOf(chosen)));
}
}
