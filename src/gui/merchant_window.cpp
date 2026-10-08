#include "merchant_window.hpp"
#include "editorial_chrome.hpp"
#include "document_preview.hpp"
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
#include <functional>
#include <algorithm>
namespace cps::gui {
using namespace merchant;
class MerchantButton : public QPushButton {
public:
    explicit MerchantButton(QWidget* parent):QPushButton(parent){setAttribute(Qt::WA_Hover);}
protected:
    void paintEvent(QPaintEvent*) override {
        if(!isEnabled()||(!underMouse()&&!isDown()))return;
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(Qt::NoPen);
        // A soft hover tint fades towards the edge. Pointer focus never paints a border.
        for(int inset=0;inset<3;++inset){painter.setBrush(QColor(217,62,41,isDown()?6:4));painter.drawRoundedRect(QRectF(rect()).adjusted(inset*2,inset*2,-inset*2,-inset*2),6,6);}
    }
    void enterEvent(QEnterEvent* event) override {QPushButton::enterEvent(event);update();}
    void leaveEvent(QEvent* event) override {QPushButton::leaveEvent(event);update();}
    void mousePressEvent(QMouseEvent* event) override {dragOrigin_=event->position().toPoint();QPushButton::mousePressEvent(event);}
    void mouseMoveEvent(QMouseEvent* event) override {
        const auto id=property("dragOrderId").toString();
        if((event->buttons()&Qt::LeftButton)&&!id.isEmpty()&&(event->position().toPoint()-dragOrigin_).manhattanLength()>=QApplication::startDragDistance()){
            setDown(false);auto* drag=new QDrag(this);auto* mime=new QMimeData;mime->setData("application/x-paperroom-order",id.toUtf8());drag->setMimeData(mime);drag->setPixmap(grab());drag->setHotSpot(dragOrigin_);drag->exec(Qt::MoveAction);return;
        }
        QPushButton::mouseMoveEvent(event);
    }
private:
    QPoint dragOrigin_;
};
class MerchantCanvas : public QWidget {
public:
    MerchantCanvas(MerchantWindow* owner,MerchantData* data):QWidget(owner),data_(data) {
        if(data_->snapshot().showcase)selection.device=1;
        setObjectName("merchantCanvas");setMouseTracking(true);setAcceptDrops(true);setAttribute(Qt::WA_OpaquePaintEvent);
        search_=new QLineEdit(this);search_->setObjectName("merchantSearch");search_->setPlaceholderText(QStringLiteral("搜索订单 / 文件名"));
        search_->setAccessibleName(QStringLiteral("搜索订单、文件名或学号"));
        search_->setStyleSheet("QLineEdit{border:none;background:transparent;color:#0B1B3B;font-family:'Microsoft YaHei';font-size:14px;}QLineEdit:focus{border-bottom:1px solid #D93E29;}");
        QObject::connect(search_,&QLineEdit::textChanged,this,[this](const QString& value){selection.search=value;selection.offset=0;refresh();});
        QObject::connect(data_,&MerchantData::changed,this,[this]{refresh();});
        const EditorialWindowButton::Action actions[]{EditorialWindowButton::Minimize,EditorialWindowButton::Maximize,EditorialWindowButton::Close};
        const QStringList names{"windowMinimize","windowMaximize","windowClose"};
        for(int i=0;i<3;++i){auto* control=new EditorialWindowButton(owner,actions[i],names[i]);control->setParent(this);control->setFocusPolicy(Qt::TabFocus);windowControls_<<control;}
    }
    int page=0;Selection selection;
    std::function<void(const QString&,const QString&)> invoked;
    QString toast;bool toastProblem=false;
    void refresh() {
        const auto& state=data_->snapshot();
        if(selection.order>=state.orders.size())selection.order=std::max(0,int(state.orders.size())-1);
        if(selection.device>=state.devices.size())selection.device=std::max(0,int(state.devices.size())-1);
        QImage scratch(1,1,QImage::Format_RGB32);QPainter painter(&scratch);QVector<Hit> controls;paintScene(painter,controls);
        QSet<QString> active;
        const qreal sx=width()/1586.,sy=height()/992.;
        for(const auto& hit:controls) {
            if(hit.action=="search")continue;
            const QString key=hit.action+"\n"+hit.value+"\n"+hit.label;active.insert(key);
            auto* button=buttons_.value(key,nullptr);
            if(!button) {button=new MerchantButton(this);buttons_.insert(key,button);button->setObjectName("merchant_"+hit.action+"_"+hit.value);
                button->setAccessibleName(hit.label);button->setToolTip(hit.label);button->setCursor(Qt::PointingHandCursor);button->setFocusPolicy(Qt::StrongFocus);
                QObject::connect(button,&QPushButton::clicked,this,[this,hit]{if(invoked)invoked(hit.action,hit.value);});}
            if(hit.action=="select-order"&&!button->property("orderMenuWired").toBool()){button->setProperty("orderMenuWired",true);button->setContextMenuPolicy(Qt::CustomContextMenu);QObject::connect(button,&QWidget::customContextMenuRequested,this,[this,hit]{if(invoked)invoked("order-menu",hit.value);});}
            button->setGeometry(QRect(qRound(hit.rect.x()*sx),qRound(hit.rect.y()*sy),qRound(hit.rect.width()*sx),qRound(hit.rect.height()*sy)));button->show();
            const int row=hit.value.toInt();const bool orderHit=hit.action=="select-order"&&row>=0&&row<state.orders.size();
            button->setProperty("dropOrderId",orderHit?state.orders[row].id:QString());
            button->setProperty("dragOrderId",orderHit&&page==1&&state.showcase&&state.orders[row].state==QStringLiteral("待打印")?state.orders[row].id:QString());
            const bool unsupported=!state.showcase&&(hit.action=="assign"||hit.action=="transfer"||hit.action=="reorder"||hit.action=="test-page");
            button->setEnabled(!unsupported);
            if(unsupported)button->setToolTip(QStringLiteral("共享后台尚未提供该手动操作；可在店铺菜单选择自动调度策略。"));
        }
        for(auto i=buttons_.begin();i!=buttons_.end();) {if(!active.contains(i.key())) {delete i.value();i=buttons_.erase(i);}else ++i;}
        search_->setGeometry(qRound(826*sx),qRound(317*sy),qRound(180*sx),qRound(33*sy));search_->setVisible(page==0);search_->raise();
        for(int i=0;i<windowControls_.size();++i){windowControls_[i]->setGeometry(qRound((1447+i*46)*sx),qRound(3*sy),qRound(43*sx),qRound(49*sy));windowControls_[i]->show();windowControls_[i]->raise();}update();
    }
    void focusSearch() {page=0;refresh();search_->setFocus();}
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {if(page==1&&data_->snapshot().showcase&&event->mimeData()->hasFormat("application/x-paperroom-order"))event->acceptProposedAction();}
    void dragMoveEvent(QDragMoveEvent* event) override {if(page==1&&data_->snapshot().showcase&&event->mimeData()->hasFormat("application/x-paperroom-order"))event->acceptProposedAction();}
    void dropEvent(QDropEvent* event) override {
        if(page!=1||!data_->snapshot().showcase)return;
        const auto source=QString::fromUtf8(event->mimeData()->data("application/x-paperroom-order"));
        for(auto* button:buttons_){const auto target=button->property("dropOrderId").toString();if(!target.isEmpty()&&button->geometry().contains(event->position().toPoint())){data_->execute("move-before",source+":"+target);event->acceptProposedAction();return;}}
    }
    void wheelEvent(QWheelEvent* event) override {if(page==0){int maximum=std::max(0,int(visibleOrders(data_->snapshot(),selection).size())-5);selection.offset=std::clamp(selection.offset+(event->angleDelta().y()<0?1:-1),0,maximum);}else if(page==1){selection.queueOffset=std::max(0,selection.queueOffset+(event->angleDelta().y()<0?1:-1));}refresh();event->accept();}
    void resizeEvent(QResizeEvent* event) override {QWidget::resizeEvent(event);refresh();}
    void paintEvent(QPaintEvent*) override {QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.scale(width()/1586.,height()/992.);QVector<Hit> controls;paintScene(painter,controls);}
private:
    void paintScene(QPainter& p,QVector<Hit>& controls) {
        const auto& state=data_->snapshot();Painter d(p,controls,assets_);
        p.fillRect(QRectF(0,0,1586,992),Qt::white);
        p.save();p.setOpacity(.18);p.drawTiledPixmap(QRectF(0,0,1586,992),QPixmap::fromImage(assets_.texture));p.restore();
        const int heroBottom=page==3?315:page==2?286:292;
        p.drawTiledPixmap(QRectF(0,56,1134,heroBottom-56),QPixmap::fromImage(assets_.texture));
        d.art("coffee",{905,57,228,155});d.art("leaf",{0,56,55,145});
        p.setPen(QPen(ruleColor,1));p.drawLine(QPointF(1145,65),QPointF(1145,852));
        if(page==0)paintWorkbench(d,state,selection);else if(page==1)paintQueue(d,state,selection);else if(page==2)paintDevices(d,state,selection);else paintStats(d,state,selection);
        p.fillRect(QRectF(0,0,1586,56),QColor("#FAFAF9"));d.rule(0,55,1586);
        p.drawImage(QRectF(39,10,41,35),QImage(":/editorial/brand-128.png"));
        d.text(91,4,62,48,QStringLiteral("纸间"),28,navy,true,true);p.setPen(ruleColor);p.drawLine(163,16,163,40);d.text(181,4,92,48,QStringLiteral("店主端"),21,slate);
        const QStringList nav{QStringLiteral("订单工作台"),QStringLiteral("打印队列"),QStringLiteral("设备管理"),QStringLiteral("营业统计")};
        const int positions[4]={562,706,826,947};
        for(int i=0;i<4;++i) {d.text(positions[i],0,i==0?108:96,55,nav[i],21,i==page?navy:slate,i==page);d.hit({qreal(positions[i]-6),0.,qreal(i==0?114:106),56.},"nav",QString::number(i),nav[i]);
            if(i==page){p.fillRect(QRectF(positions[i]-2,52,i==0?100:88,4),red);}}
        d.icon("shop",1240,18,23);d.text(1275,3,140,50,QStringLiteral("校园一店⌄"),20);d.hit({1227,1,205,52},"shop-menu",{},QStringLiteral("店铺菜单"));
        p.fillRect(QRectF(0,942,1586,50),Qt::white);d.rule(39,942,1507);
        d.dot(44,963,state.connected||state.showcase?QStringLiteral("店铺营业中"):QStringLiteral("连接后台中"),state.connected||state.showcase?green:slate);
        const auto online=std::count_if(state.devices.cbegin(),state.devices.cend(),[](const Device& device){return device.online;});
        d.text(175,946,151,40,"06:00 - 23:00",15,slate);d.text(344,946,196,40,QStringLiteral("%1 台设备在线").arg(online),15,slate);
        if(state.showcase)d.text(556,946,215,40,QStringLiteral("样例模式 · 独立演示"),14,slate);
        d.text(1331,946,221,40,state.date+" "+state.clock,14,slate,false,false,Qt::AlignRight|Qt::AlignVCenter);
        if(!toast.isEmpty()) {d.panel({480,795,625,51},toastProblem?QColor("#FFF2EE"):QColor("#F5F7FA"),toastProblem?red:ruleColor,8);d.text(499,797,590,47,toast,16,toastProblem?red:navy);}
    }
    MerchantData* data_;Assets assets_;QHash<QString,QPushButton*> buttons_;QLineEdit* search_;QVector<EditorialWindowButton*> windowControls_;
};
static void styleMerchantMenu(QMenu& menu) {
    menu.setStyleSheet("QMenu{background:#FAFAF9;color:#14213E;border:1px solid #DADDE2;border-radius:8px;padding:6px;font-family:'Microsoft YaHei';font-size:14px;}QMenu::item{padding:9px 24px 9px 15px;border-radius:5px;}QMenu::item:selected{background:#FBEDE8;color:#D93E29;}QMenu::separator{height:1px;background:#DADDE2;margin:6px 10px;}");
}
MerchantWindow::MerchantWindow(WindowOptions options,bool showcase):model_(new MerchantData(options,showcase,this)),canvas_(new MerchantCanvas(this,model_)) {
    QIcon icon;for(int size:{16,24,32,48,64,128,256})icon.addFile(QStringLiteral(":/merchant/brand-%1.png").arg(size),QSize(size,size));
    setObjectName("merchantWindow");setWindowTitle(QStringLiteral("纸间 · 店主端"));setWindowIcon(icon);setCentralWidget(canvas_);
    setMinimumSize(950,594);resize(1269,794);new EditorialChrome(this,56,992);
    canvas_->invoked=[this](const QString& a,const QString& v){action(a,v);};
    connect(model_,&MerchantData::feedback,this,&MerchantWindow::notify);connect(model_,&MerchantData::backendReady,this,&MerchantWindow::backendReady);
    for(int i=0;i<4;++i){auto* key=new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i+1)),this);connect(key,&QShortcut::activated,this,[this,i]{setPage(i);});}
    auto* find=new QShortcut(QKeySequence::Find,this);connect(find,&QShortcut::activated,this,[this]{canvas_->focusSearch();});
    auto* refresh=new QShortcut(QKeySequence("Ctrl+R"),this);connect(refresh,&QShortcut::activated,this,[this]{model_->execute("refresh");});
    QTimer::singleShot(0,model_,&MerchantData::start);
}
void MerchantWindow::setPage(int index) {
    deviceSelections_[canvas_->page]=canvas_->selection.device;
    canvas_->page=std::clamp(index,0,3);canvas_->selection.device=deviceSelections_[canvas_->page];canvas_->refresh();
}
void MerchantWindow::notify(const QString& value,bool problem) {canvas_->toast=value;canvas_->toastProblem=problem;canvas_->update();QTimer::singleShot(4800,canvas_,[this,value]{if(canvas_->toast==value){canvas_->toast.clear();canvas_->update();}});}
void MerchantWindow::captureAll(const QString& directory) {
    QDir().mkpath(directory);resize(qRound(1586/devicePixelRatioF()),qRound(992/devicePixelRatioF()));const QStringList names{"workbench","queue","devices","stats"};
    for(int i=0;i<4;++i){setPage(i);QApplication::processEvents();if(auto* focus=QApplication::focusWidget())focus->clearFocus();canvas_->grab().toImage().scaled(1586,992,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(QDir(directory).filePath(names[i]+".png"));}setPage(0);
}
void MerchantWindow::viewFile() {
    const auto& state=model_->snapshot();if(state.orders.isEmpty())return;const auto& order=state.orders[canvas_->selection.order];
    if(!order.path.isEmpty()&&QFileInfo::exists(order.path)){QDesktopServices::openUrl(QUrl::fromLocalFile(order.path));return;}
    auto* dialog=new EditorialDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);auto* layout=new QVBoxLayout(dialog);layout->setContentsMargins(0,0,0,0);layout->addWidget(editorialDialogHeader(dialog,QStringLiteral("文件预览"),order.title));
    auto* note=new QLabel(state.showcase?QStringLiteral("样例封面预览；实际订单文件保留在提交文件的学生端。后台当前共享订单元数据。"):QStringLiteral("该订单未共享源文件路径。请选择本机文件进行核对，订单参数保持原样。"));note->setWordWrap(true);note->setContentsMargins(20,10,20,10);layout->addWidget(note);
    auto* paper=new QLabel;paper->setAlignment(Qt::AlignCenter);QImage image(420,500,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter p(&image);QVector<Hit> hits;Assets art;Painter d(p,hits,art);d.cover({20,10,380,455});p.end();paper->setPixmap(QPixmap::fromImage(image));layout->addWidget(paper);
    auto* pick=new QPushButton(QStringLiteral("选择本机文件核对"));connect(pick,&QPushButton::clicked,dialog,[this]{const auto file=QFileDialog::getOpenFileName(this,QStringLiteral("核对本机订单文件"));if(!file.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(file));});layout->addWidget(pick);dialog->resize(550,650);dialog->setWindowModality(Qt::WindowModal);dialog->open();
}
void MerchantWindow::action(const QString& name,const QString& value) {
    if(name=="nav"){setPage(value.toInt());return;}if(name=="select-order"){canvas_->selection.order=value.toInt();canvas_->refresh();return;}
    if(name=="select-device"){canvas_->selection.device=value.toInt();canvas_->refresh();return;}if(name=="filter"){canvas_->selection.filter=value.toInt();canvas_->selection.offset=0;canvas_->refresh();return;}
    if(name=="table-prev"||name=="table-next"){int maximum=std::max(0,int(visibleOrders(model_->snapshot(),canvas_->selection).size())-5);canvas_->selection.offset=std::clamp(canvas_->selection.offset+(name=="table-next"?5:-5),0,maximum);canvas_->refresh();return;}
    if(name=="queue-prev"||name=="queue-next"){canvas_->selection.queueOffset=std::max(0,canvas_->selection.queueOffset+(name=="queue-next"?1:-1));canvas_->refresh();return;}
    if(name=="view-file"){viewFile();return;}if(name=="order-details"){setPage(0);return;}
    if(name=="range") {QString range=value==QStringLiteral("今天")?"today":value==QStringLiteral("本周")?"week":value==QStringLiteral("本月")?"month":value;if(range==QStringLiteral("自定义")){QString date=QInputDialog::getText(this,QStringLiteral("统计日期"),QStringLiteral("输入日期（yyyy-MM-dd）"));if(date.isEmpty())return;if(!QDate::fromString(date,Qt::ISODate).isValid()){notify(QStringLiteral("请输入有效的 yyyy-MM-dd 日期"),true);return;}range=date;}canvas_->selection.range=value;model_->setRange(range);canvas_->refresh();return;}
    if(name=="export-report") {QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出营业报表"),"营业报表.json","JSON (*.json)");if(!path.isEmpty())model_->exportReport(path);return;}
    const auto& state=model_->snapshot();
    if(name=="order-menu"){int index=value.toInt();if(index<0||index>=state.orders.size())return;canvas_->selection.order=index;canvas_->refresh();const auto order=state.orders[index];QMenu menu(this);menu.setObjectName("merchantOrderMenu");styleMerchantMenu(menu);menu.addAction(QStringLiteral("查看文件"),this,&MerchantWindow::viewFile);if(order.state==QStringLiteral("待打印"))menu.addAction(QStringLiteral("取消订单"),this,[this,order]{model_->execute("cancel",order.id);});if(order.state==QStringLiteral("待取件")&&state.showcase)menu.addAction(QStringLiteral("确认取件"),this,[this,order]{model_->execute("pickup",order.id);});menu.exec(QCursor::pos());return;}
    if(name=="shop-menu") {QMenu menu(this);menu.setObjectName("merchantShopMenu");styleMerchantMenu(menu);menu.addAction(QStringLiteral("订单工作台"),this,[this]{setPage(0);});menu.addAction(QStringLiteral("刷新后台"),this,[this]{model_->execute("refresh");});if(!state.showcase){menu.addSeparator();auto* policy=menu.addMenu(QStringLiteral("自动调度策略"));const QStringList labels{QStringLiteral("先来先服务"),QStringLiteral("短作业优先"),QStringLiteral("优先级优先"),QStringLiteral("期限优先"),QStringLiteral("收入优先")};const QStringList values{"fcfs","sjf","priority","deadline","revenue"};for(int i=0;i<5;++i)policy->addAction(labels[i],this,[this,values,i]{model_->execute("policy",values[i]);});menu.addAction(state.paused?QStringLiteral("恢复全部派发"):QStringLiteral("暂停全部派发"),this,[this]{model_->execute(model_->snapshot().paused?"resume-all":"pause-all");});}auto* trigger=canvas_->findChild<QPushButton*>("merchant_shop-menu_");menu.exec(trigger?trigger->mapToGlobal(QPoint(0,trigger->height()+4)):QCursor::pos());return;}
    if(name=="pickup-slip") {if(state.orders.isEmpty())return;const auto& o=state.orders[canvas_->selection.order];QString path=QFileDialog::getSaveFileName(this,QStringLiteral("保存取件单"),o.id+"-取件单.txt","Text (*.txt)");if(path.isEmpty())return;QSaveFile file(path);if(file.open(QIODevice::WriteOnly)){file.write((QStringLiteral("纸间 · 校园一店\n订单：%1\n文件：%2\n学号：%3\n规格：%4\n金额：%5\n取件时间：%6\n").arg(o.id,o.title,o.student,specs(o),money(o.amountCents),o.pickupTime)).toUtf8());file.commit();notify(QStringLiteral("取件单已保存"));}return;}
    if(name=="device-settings"||name=="add-device") {
        const bool adding=name=="add-device"&&state.showcase;
        if(adding&&state.devices.size()>=6){notify(QStringLiteral("样例最多支持六台设备。"),true);return;}
        int index=value.isEmpty()?canvas_->selection.device:value.toInt();
        if(name=="add-device"&&!state.showcase){QStringList deviceSlots;for(const auto& d:state.devices)deviceSlots<<d.name+(d.online?QStringLiteral("（在线）"):QStringLiteral("（离线）"));bool ok=false;auto chosen=QInputDialog::getItem(this,QStringLiteral("配置设备槽位"),QStringLiteral("共享后台提供六个固定槽位，请选择要启用或重新配置的设备。"),deviceSlots,0,false,&ok);if(!ok)return;index=deviceSlots.indexOf(chosen);}
        if(!adding&&(index<0||index>=state.devices.size()))return;
        const auto device=adding?Device{}:state.devices[index];
        auto* dialog=new EditorialDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);auto* root=new QVBoxLayout(dialog);root->setContentsMargins(0,0,0,0);root->addWidget(editorialDialogHeader(dialog,QStringLiteral("设备设置"),device.name));auto* form=new QFormLayout;form->setContentsMargins(24,20,24,20);
        auto* color=new QCheckBox(QStringLiteral("支持彩色"));color->setChecked(device.color);auto* duplex=new QCheckBox(QStringLiteral("支持双面"));duplex->setChecked(device.duplex);auto* a3=new QCheckBox(QStringLiteral("支持 A3"));a3->setChecked(device.a3);auto* enabled=new QCheckBox(QStringLiteral("自动接单"));enabled->setChecked(device.enabled);
        auto* deviceName=new QLineEdit(adding?QStringLiteral("新打印机 %1").arg(state.devices.size()+1):device.name);form->addRow(QStringLiteral("设备名称"),deviceName);
        form->addRow(QStringLiteral("色彩"),color);form->addRow(QStringLiteral("打印方式"),duplex);form->addRow(QStringLiteral("纸张"),a3);form->addRow(QStringLiteral("接单状态"),enabled);root->addLayout(form);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,index,adding,deviceName,color,duplex,a3,enabled]{if(deviceName->text().trimmed().isEmpty()){deviceName->setFocus();return;}int target=index;if(adding){target=model_->snapshot().devices.size();model_->addSampleDevice(deviceName->text(),color->isChecked());}model_->setDevice(target,{{"name",deviceName->text().trimmed()},{"color",color->isChecked()},{"duplex",duplex->isChecked()},{"a3",a3->isChecked()},{"enabled",enabled->isChecked()}});dialog->accept();});dialog->setWindowModality(Qt::WindowModal);dialog->resize(500,380);dialog->open();return;
    }
    if(name=="assign"||name=="transfer"||name=="reorder") {int orderIndex=value.isEmpty()?canvas_->selection.order:value.toInt();if(orderIndex<0||orderIndex>=state.orders.size())return;const auto& o=state.orders[orderIndex];
        if(name=="reorder"){QStringList list{QStringLiteral("向前调整一位"),QStringLiteral("向后调整一位")};bool ok=false;auto chosen=QInputDialog::getItem(this,QStringLiteral("调整顺序"),o.title,list,0,false,&ok);if(ok)model_->execute(name,o.id+":"+(chosen==list[0]?"up":"down"));return;}
        QStringList names;for(const auto& d:state.devices)names<<d.name;bool ok=false;auto chosen=QInputDialog::getItem(this,QStringLiteral("分配打印设备"),o.title,names,std::max(0,canvas_->selection.device),false,&ok);if(ok)model_->execute(name,o.id+":"+QString::number(names.indexOf(chosen)));return;
    }
    if(name=="restock"){
        auto* dialog=new EditorialDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);auto* root=new QVBoxLayout(dialog);root->setContentsMargins(0,0,0,0);root->addWidget(editorialDialogHeader(dialog,QStringLiteral("耗材与补纸"),state.showcase?QStringLiteral("样例补纸将同步增加各设备的纸张余量"):QStringLiteral("增加共享后台库存；墨粉和单机纸量尚未接入遥测")));
        auto* form=new QFormLayout;form->setContentsMargins(24,20,24,20);auto* paper=new QComboBox;paper->addItems({"A4","A3"});auto* quantity=new QSpinBox;quantity->setRange(1,1000000);quantity->setValue(500);quantity->setSuffix(QStringLiteral(" 张"));form->addRow(QStringLiteral("纸张规格"),paper);form->addRow(QStringLiteral("补充数量"),quantity);root->addLayout(form);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);root->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,paper,quantity]{model_->execute("restock",paper->currentText().toLower()+":"+QString::number(quantity->value()));dialog->accept();});dialog->resize(530,260);dialog->setWindowModality(Qt::WindowModal);dialog->open();return;
    }
    model_->execute(name,value);
}
}
