#include "merchant_data.hpp"
#include "cps/runtime/state_store.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace cps::gui::merchant {
namespace {
QString serviceText(const QString& value) {
    if (value == QLatin1String("express")) return QStringLiteral("快速");
    if (value == QLatin1String("flexible")) return QStringLiteral("灵活");
    return QStringLiteral("普通");
}
QString stateText(JobState state) {
    switch (state) {
    case JobState::Queued: return QStringLiteral("待打印");
    case JobState::Running: return QStringLiteral("打印中");
    case JobState::Cancelling: return QStringLiteral("取消中");
    case JobState::Completed: return QStringLiteral("已完成");
    case JobState::Cancelled: return QStringLiteral("已取消");
    case JobState::Failed: return QStringLiteral("失败");
    }
    return QStringLiteral("未知");
}
bool printedState(const Order& order) {
    return order.state == QStringLiteral("已完成") || order.state == QStringLiteral("待取件");
}
int faces(const Order& order) { return order.pages * order.copies; }
int paperSheets(const Order& order) {
    return (order.duplex ? (order.pages + 1) / 2 : order.pages) * order.copies;
}
qint64 rangeStart(const QString& range, const QDateTime& now) {
    QDate date = now.date();
    if (range == QLatin1String("month")) date = QDate(date.year(), date.month(), 1);
    else if (range == QLatin1String("week")) date = date.addDays(1 - date.dayOfWeek());
    else if (QDate::fromString(range, Qt::ISODate).isValid()) date = QDate::fromString(range, Qt::ISODate);
    return QDateTime(date, QTime(0, 0)).toMSecsSinceEpoch();
}
bool compatible(const Order& order, const Device& device) {
    return (!order.color || device.color) && (!order.a3 || device.a3) && (!order.duplex || device.duplex);
}
}

MerchantData::MerchantData(WindowOptions options, bool showcase, QObject* parent)
    : QObject(parent), options_(std::move(options)) {
    snapshot_.showcase = showcase;
    const auto now = QDateTime::currentDateTime();
    snapshot_.date = now.toString(QStringLiteral("yyyy年M月d日"));
    snapshot_.clock = now.toString(QStringLiteral("HH:mm"));
    snapshot_.connection = QStringLiteral("正在连接持久后台…");
    if (showcase) {
        seedShowcase();
        simulation_ = new QTimer(this);
        simulation_->setInterval(600);
        connect(simulation_, &QTimer::timeout, this, [this] {
            bool active = false;
            for (auto& device : snapshot_.devices) {
                if (!property(qPrintable(QStringLiteral("simulation_active_%1").arg(device.index))).toBool() || !device.enabled) continue;
                active = true;
                if (!device.busy && device.online) {
                    for (auto& order : snapshot_.orders) {
                        if (order.state != QStringLiteral("待打印") || order.printer != device.index || !compatible(order, device)) continue;
                        if (device.paper < paperSheets(order)) {
                            device.enabled = false;
                            emit feedback(QStringLiteral("样例：设备缺纸，请补纸后继续。"), true);
                            break;
                        }
                        order.state = QStringLiteral("打印中");
                        device.busy = true; device.jobId = order.id;
                        device.printed = order.printed; device.total = faces(order);
                        device.paper -= paperSheets(order);
                        break;
                    }
                }
                if (!device.busy) continue;
                for (auto& order : snapshot_.orders) {
                    if (order.id != device.jobId) continue;
                    order.printed = std::min(faces(order), order.printed + 1);
                    device.printed = order.printed;
                    if (order.printed == faces(order)) {
                        order.state = QStringLiteral("待取件");
                        order.finishedMs = QDateTime(QDate(2026, 10, 3), QTime::fromString(snapshot_.clock, QStringLiteral("HH:mm"))).toMSecsSinceEpoch();
                        device.busy = false; device.jobId.clear(); device.printed = 0; device.total = 0;
                        emit feedback(QStringLiteral("样例：%1 已打印完成，等待取件。").arg(order.id), false);
                    }
                    break;
                }
            }
            if (!active) simulation_->stop();
            recount(); emit changed();
        });
    } else {
        qRegisterMetaType<ViewState>();
        controller_ = new PrintController(options_);
        controller_->moveToThread(&worker_);
        connect(&worker_, &QThread::started, controller_, &PrintController::initialize);
        connect(&worker_, &QThread::finished, controller_, &QObject::deleteLater);
        connect(controller_, &PrintController::viewChanged, this, &MerchantData::apply, Qt::QueuedConnection);
        connect(controller_, &PrintController::feedback, this, &MerchantData::feedback, Qt::QueuedConnection);
    }
}

MerchantData::~MerchantData() {
    if (simulation_) simulation_->stop();
    if (controller_ && worker_.isRunning()) {
        // Close only this client's socket. The coordinator and its jobs keep running.
        QMetaObject::invokeMethod(controller_, &PrintController::requestClose, Qt::BlockingQueuedConnection);
        worker_.quit(); worker_.wait();
    } else if (controller_) {
        // No worker was started, so destruction can remain on this thread.
        delete controller_;
    }
}

void MerchantData::start() {
    if (started_) return;
    started_ = true;
    if (snapshot_.showcase) { emit changed(); emit backendReady(); }
    else worker_.start();
}

void MerchantData::seedShowcase() {
    snapshot_ = Snapshot{};
    snapshot_.showcase = true; snapshot_.connected = true;
    snapshot_.date = QStringLiteral("2026年10月3日"); snapshot_.clock = QStringLiteral("10:24");
    snapshot_.connection = QStringLiteral("本地交互样例 · 不连接真实后台");
    const QDate date(2026, 10, 3);
    const auto time = [date](const QString& text) { return QDateTime(date, QTime::fromString(text, QStringLiteral("HH:mm"))).toMSecsSinceEpoch(); };
    const auto add = [&](QString id, QString title, int pages, bool color, bool duplex, bool a3, QString service,
                         qint64 amount, QString pickup, int printer) {
        Order order;
        order.id = std::move(id); order.title = std::move(title); order.pages = pages;
        order.color = color; order.duplex = duplex; order.a3 = a3; order.service = std::move(service);
        order.amountCents = amount; order.pickupTime = std::move(pickup); order.deadlineMs = time(order.pickupTime);
        order.student = QStringLiteral("202400000%1").arg(snapshot_.orders.size() + 1);
        order.printer = printer; order.submittedMs = time(QStringLiteral("10:18"));
        snapshot_.orders.append(order);
    };
    add(QStringLiteral("P1024"), QStringLiteral("操作系统实验报告.pdf"), 8, true, true, false, QStringLiteral("普通"), 1140, QStringLiteral("10:32"), 1);
    add(QStringLiteral("P1025"), QStringLiteral("英语课程论文.docx"), 12, false, true, false, QStringLiteral("快速"), 540, QStringLiteral("10:35"), 0);
    add(QStringLiteral("P1026"), QStringLiteral("高等数学复习资料.pdf"), 24, false, true, false, QStringLiteral("灵活"), 720, QStringLiteral("11:00"), 0);
    add(QStringLiteral("P1027"), QStringLiteral("社团活动海报.pdf"), 2, true, false, true, QStringLiteral("普通"), 600, QStringLiteral("10:45"), 1);
    add(QStringLiteral("P1028"), QStringLiteral("课程设计说明书.pdf"), 16, false, true, false, QStringLiteral("快速"), 720, QStringLiteral("10:40"), 0);
    add(QStringLiteral("P1029"), QStringLiteral("毕业论文终稿.pdf"), 32, false, true, false, QStringLiteral("普通"), 960, QStringLiteral("10:50"), -1);
    const QStringList students{QStringLiteral("2024000001"), QStringLiteral("2024000018"), QStringLiteral("2024000032"),
        QStringLiteral("2024000056"), QStringLiteral("2024000071"), QStringLiteral("2024000086")};
    for (int i = 0; i < students.size(); ++i) snapshot_.orders[i].student = students[i];
    snapshot_.orders.move(4, 2);
    add(QStringLiteral("P1022"), QStringLiteral("实验资料汇编.pdf"), 24, false, true, false, QStringLiteral("普通"), 720, QStringLiteral("10:30"), 0);
    snapshot_.orders.last().state = QStringLiteral("打印中"); snapshot_.orders.last().printed = 18;
    const std::array<int, 4> groups{{7, 11, 13, 10}};
    const std::array<qint64, 4> totals{{4240, 8860, 10680, 8900}};
    const QStringList titles{QStringLiteral("课堂讲义.pdf"), QStringLiteral("课程复习资料.pdf"), QStringLiteral("实验记录.pdf"), QStringLiteral("活动手册.pdf")};
    int historyIndex = 0;
    for (int group = 0; group < 4; ++group) {
        for (int row = 0; row < groups[group]; ++row, ++historyIndex) {
            Order order;
            order.id = QStringLiteral("P%1").arg(980 + historyIndex); order.title = titles[historyIndex % titles.size()];
            order.student = QStringLiteral("样例客户 %1").arg(historyIndex + 1);
            order.pages = historyIndex == 40 ? 26 : 12; order.color = historyIndex < 8;
            order.printer = order.color ? 1 : 0; order.printed = order.pages;
            order.state = historyIndex < 12 ? QStringLiteral("待取件") : QStringLiteral("已完成");
            order.service = historyIndex < 10 ? QStringLiteral("快速") : historyIndex < 34 ? QStringLiteral("普通") : QStringLiteral("灵活");
            order.amountCents = totals[group] / groups[group] + (row < totals[group] % groups[group] ? 1 : 0);
            const QTime finished(7 + group, group == 3 ? row * 2 : row * 4);
            order.finishedMs = QDateTime(date, finished).toMSecsSinceEpoch(); order.submittedMs = order.finishedMs - 252000;
            order.deadlineMs = order.finishedMs + 600000; order.pickupTime = finished.addSecs(600).toString(QStringLiteral("HH:mm"));
            snapshot_.orders.append(order);
        }
    }
    Device mono; mono.index = 0; mono.name = QStringLiteral("黑白打印机 01"); mono.color = false;
    mono.a3 = true; mono.busy = true; mono.jobId = QStringLiteral("P1022"); mono.printed = 18; mono.total = 24;
    mono.paper = 184; mono.toner = {{18, -1, -1, -1}};
    Device color; color.index = 1; color.name = QStringLiteral("彩色打印机 02"); color.color = true;
    color.paper = 260;
    snapshot_.devices = {mono, color};
    recount();
}

void MerchantData::apply(const ViewState& state) {
    const bool becameReady = !snapshot_.connected && state.connected;
    lastState_ = state;
    Snapshot next;
    next.connected = state.connected; next.paused = state.paused; next.connection = state.connection;
    const auto now = QDateTime::currentDateTime();
    next.date = now.toString(QStringLiteral("yyyy年M月d日")); next.clock = now.toString(QStringLiteral("HH:mm"));
    next.clearMinutes = state.queueClearMs >= state.nowMs
        ? static_cast<int>(std::ceil(static_cast<double>(state.queueClearMs - state.nowMs) / 60000.0)) : -1;
    QHash<QString, QJsonObject> raw;
    for (const auto& value : state.shop.value(QStringLiteral("jobs")).toArray()) {
        const auto object = value.toObject(); raw.insert(object.value(QStringLiteral("job_id")).toString(), object);
    }
    QHash<QString, Order> orders;
    for (const auto& job : state.jobs) {
        const auto object = raw.value(job.id);
        Order order; order.id = job.id; order.title = job.title; order.student = job.customer;
        order.pages = object.value(QStringLiteral("pages")).toInt(job.sheets);
        order.copies = object.value(QStringLiteral("copies")).toInt(1); order.printed = job.printed;
        // Waiting order is global: assigned_printer on a retry is an audit field,
        // not a promise that the next attempt belongs to the same device.
        order.printer = job.state == JobState::Queued ? -1 : job.printer;
        if (job.state == JobState::Running || job.state == JobState::Cancelling)
            order.printer = object.value(QStringLiteral("slot")).toInt(job.printer);
        order.color = object.value(QStringLiteral("color")).toBool();
        order.duplex = object.value(QStringLiteral("duplex")).toBool(); order.a3 = object.value(QStringLiteral("paper")).toString() == QLatin1String("a3");
        order.service = serviceText(object.value(QStringLiteral("service")).toString()); order.state = stateText(job.state);
        order.amountCents = job.quotedCents; order.deadlineMs = static_cast<qint64>(object.value(QStringLiteral("deadline_ms")).toDouble());
        order.submittedMs = static_cast<qint64>(object.value(QStringLiteral("submitted_ms")).toDouble());
        order.finishedMs = static_cast<qint64>(object.value(QStringLiteral("finished_ms")).toDouble());
        order.pickupTime = order.deadlineMs > 0 ? QDateTime::fromMSecsSinceEpoch(order.deadlineMs).toString(QStringLiteral("HH:mm")) : QStringLiteral("—");
        order.estimatedMinutes=printedState(order)?0:job.estimatedFinishMs>=state.nowMs&&!(state.paused&&job.state==JobState::Queued)?static_cast<int>((job.estimatedFinishMs-state.nowMs+59999)/60000):-1;
        // Existing IPC contains metadata only; no uploaded source path is available.
        if (printedState(order)) order.printed = faces(order);
        orders.insert(order.id, order);
    }
    QSet<QString> added;
    for (const auto& id : state.waitingOrder) {
        if (orders.contains(id)) { next.orders.append(orders.value(id)); added.insert(id); }
    }
    for (const auto& job : state.jobs) if (!added.contains(job.id)) next.orders.append(orders.value(job.id));
    const auto fleet = state.shop.value(QStringLiteral("fleet")).toArray();
    for (int i = 0; i < static_cast<int>(state.printers.size()); ++i) {
        const auto config = i < fleet.size() ? fleet[i].toObject() : QJsonObject{};
        const auto& printer = state.printers[static_cast<std::size_t>(i)];
        Device device; device.index = i; device.name = config.value(QStringLiteral("name")).toString(QStringLiteral("打印机 %1").arg(i + 1));
        device.online = config.value(QStringLiteral("online")).toBool(true); device.enabled = device.online;
        device.color = config.value(QStringLiteral("color")).toBool(); device.duplex = config.value(QStringLiteral("duplex")).toBool();
        device.a3 = config.value(QStringLiteral("a3")).toBool(); device.busy = !printer.jobId.isEmpty();
        device.jobId = printer.jobId; device.printed = printer.printed; device.total = printer.total;
        device.paper = -1; device.capacity = -1; device.toner = {{-1, -1, -1, -1}};
        next.devices.append(device);
    }
    snapshot_ = std::move(next); recount(); emit changed();
    if (becameReady) emit backendReady();
}

void MerchantData::recount() {
    snapshot_.todayOrders = 0; snapshot_.waiting = 0; snapshot_.printing = 0; snapshot_.ready = 0; snapshot_.completed = 0;
    snapshot_.printedPages = 0; snapshot_.blackPages = 0; snapshot_.colorPages = 0; snapshot_.revenueCents = 0;
    snapshot_.averageMinutes = 0; snapshot_.serviceCounts = {{0, 0, 0}};
    for (auto& device : snapshot_.devices) { device.queued = 0; device.todayPages = 0; }
    const auto now = snapshot_.showcase ? QDateTime(QDate(2026, 10, 3), QTime(10, 24)) : QDateTime::currentDateTime();
    const auto customDate = QDate::fromString(range_, Qt::ISODate);
    const qint64 begin = rangeStart(range_, now);
    const qint64 end = customDate.isValid() ? QDateTime(customDate.addDays(1), QTime(0, 0)).toMSecsSinceEpoch()-1 : now.toMSecsSinceEpoch();
    snapshot_.rangeLabel=range_=="today"?QStringLiteral("今日"):range_=="week"?QStringLiteral("本周"):range_=="month"?QStringLiteral("本月"):QStringLiteral("指定日");
    snapshot_.rangeDate=customDate.isValid()?customDate.toString(QStringLiteral("yyyy年M月d日")):snapshot_.date;
    snapshot_.hourlyLabels = {QStringLiteral("06"), QStringLiteral("07"), QStringLiteral("08"), QStringLiteral("09"), QStringLiteral("10")};
    if (!snapshot_.showcase) {
        snapshot_.hourlyLabels.clear();
        for (int hour = 0; hour < 24; ++hour) snapshot_.hourlyLabels.append(QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0')));
    }
    snapshot_.hourlyRevenue.fill(0, snapshot_.hourlyLabels.size());
    QHash<QString, qint64> refunds;
    if (!snapshot_.showcase) for (const auto& job : lastState_.jobs) refunds.insert(job.id, job.refundCents);
    double turnaroundMs = 0; int turnaroundCount = 0;
    for (const auto& order : snapshot_.orders) {
        if (order.state == QStringLiteral("待打印")) ++snapshot_.waiting;
        if (order.state == QStringLiteral("打印中") || order.state == QStringLiteral("取消中")) ++snapshot_.printing;
        if (order.state == QStringLiteral("待取件")) ++snapshot_.ready;
        if (order.state == QStringLiteral("已完成")) ++snapshot_.completed;
        if (order.printer >= 0 && order.printer < snapshot_.devices.size() && order.state == QStringLiteral("待打印")) ++snapshot_.devices[order.printer].queued;
        const bool submittedInRange = order.submittedMs >= begin && order.submittedMs <= end;
        if (submittedInRange) {
            ++snapshot_.todayOrders;
            const int service = order.service == QStringLiteral("快速") ? 0 : order.service == QStringLiteral("灵活") ? 2 : 1;
            ++snapshot_.serviceCounts[static_cast<std::size_t>(service)];
        }
        const bool completedInRange = printedState(order) && order.finishedMs >= begin && order.finishedMs <= end;
        const int count = completedInRange ? faces(order) : submittedInRange && !printedState(order) ? order.printed : 0;
        snapshot_.printedPages += count;
        if (order.color) snapshot_.colorPages += count; else snapshot_.blackPages += count;
        const qint64 todayBegin = rangeStart(QStringLiteral("today"), now);
        const bool completedToday = printedState(order) && order.finishedMs >= todayBegin && order.finishedMs <= end;
        const bool submittedToday = order.submittedMs >= todayBegin && order.submittedMs <= end;
        const int deviceTodayCount = completedToday ? faces(order) : submittedToday && !printedState(order) ? order.printed : 0;
        if (order.printer >= 0 && order.printer < snapshot_.devices.size()) snapshot_.devices[order.printer].todayPages += deviceTodayCount;
        if (!completedInRange) continue;
        const qint64 revenue = order.amountCents - refunds.value(order.id);
        snapshot_.revenueCents += revenue;
        const auto finished = QDateTime::fromMSecsSinceEpoch(order.finishedMs);
        const int bucket = snapshot_.hourlyLabels.indexOf(finished.toString(QStringLiteral("HH")));
        if (bucket >= 0) snapshot_.hourlyRevenue[bucket] += static_cast<double>(revenue) / 100.0;
        if (order.submittedMs > 0 && order.finishedMs >= order.submittedMs) {
            turnaroundMs += static_cast<double>(order.finishedMs - order.submittedMs); ++turnaroundCount;
        }
    }
    if (turnaroundCount > 0) snapshot_.averageMinutes = turnaroundMs / (60000.0 * turnaroundCount);
    if (snapshot_.showcase) {
        int remaining = 0;
        for(const auto& order:snapshot_.orders)
            if(order.state==QStringLiteral("待打印")||order.state==QStringLiteral("打印中"))remaining+=std::max(0,faces(order)-order.printed);
        snapshot_.clearMinutes = (remaining + 8) / 9; // Sample aggregate throughput: nine faces/minute.
    }
}

void MerchantData::execute(const QString& action, const QString& value) {
    if (action == QLatin1String("refresh")) {
        if (snapshot_.showcase) { recount(); emit changed(); emit feedback(QStringLiteral("样例状态已刷新。"), false); }
        else QMetaObject::invokeMethod(controller_, &PrintController::checkConnection, Qt::QueuedConnection);
        return;
    }
    const auto fields = value.split(QLatin1Char(':'));
    bool validIndex = false; const int index = value.toInt(&validIndex);
    if (!snapshot_.showcase) {
        if (!snapshot_.connected) { emit feedback(QStringLiteral("后台未连接，操作未发送。"), true); return; }
        if (action == QLatin1String("start") || action == QLatin1String("pause") || action == QLatin1String("toggle-device")) {
            if (!validIndex || index < 0 || index >= snapshot_.devices.size()) { emit feedback(QStringLiteral("请选择有效设备。"), true); return; }
            const bool online = action == QLatin1String("toggle-device") ? !snapshot_.devices[index].online : action == QLatin1String("start");
            QMetaObject::invokeMethod(controller_, [controller = controller_, index, online] { controller->shopCommand(QStringLiteral("set_printer"), {{QStringLiteral("index"), index}, {QStringLiteral("online"), online}}); }, Qt::QueuedConnection);
            if (action == QLatin1String("pause")) emit feedback(QStringLiteral("停止设备接新单；正在打印的订单仍将完成。"), false);
        } else if (action == QLatin1String("restock")) {
            bool validQuantity = false; const int quantity = fields.value(1).toInt(&validQuantity);
            if ((fields.value(0) != QLatin1String("a4") && fields.value(0) != QLatin1String("a3")) || !validQuantity || quantity < 1 || quantity > 1000000) { emit feedback(QStringLiteral("补纸参数无效。"), true); return; }
            QMetaObject::invokeMethod(controller_, [controller = controller_, paper = fields[0], quantity] { controller->shopCommand(QStringLiteral("restock"), {{QStringLiteral("paper"), paper}, {QStringLiteral("quantity"), quantity}}); }, Qt::QueuedConnection);
        } else if (action == QLatin1String("policy")) {
            const auto parsed = cps::policyFromName(value);
            const int policy = parsed ? static_cast<int>(*parsed) : -1;
            if (policy < 0) { emit feedback(QStringLiteral("未知调度策略。"), true); return; }
            QMetaObject::invokeMethod(controller_, [controller = controller_, policy] { controller->setPolicy(policy); }, Qt::QueuedConnection);
        } else if (action == QLatin1String("cancel")) {
            QMetaObject::invokeMethod(controller_, [controller = controller_, value] { controller->cancel(value); }, Qt::QueuedConnection);
        } else if (action == QLatin1String("pause-all") || action == QLatin1String("resume-all")) {
            QMetaObject::invokeMethod(controller_, [controller = controller_, action] { controller->setPaused(action == QLatin1String("pause-all")); }, Qt::QueuedConnection);
        } else if (action == QLatin1String("preview")) {
            emit feedback(QStringLiteral("当前后台只保存订单元数据，没有上传的源文件内容，无法预览。"), true);
        } else {
            emit feedback(QStringLiteral("当前后台未提供此手动操作（%1）。可在店铺菜单选择 FCFS、SJF 或综合调度。").arg(action), true);
        }
        return;
    }
    auto fail = [this](const QString& message) { emit feedback(QStringLiteral("样例：") + message, true); };
    if (action == QLatin1String("start") || action == QLatin1String("pause") || action == QLatin1String("toggle-device") || action == QLatin1String("test-page")) {
        if (!validIndex || index < 0 || index >= snapshot_.devices.size()) { fail(QStringLiteral("请选择有效设备。")); return; }
        auto& device = snapshot_.devices[index];
        if (action == QLatin1String("start")) {
            if (!device.online) { fail(QStringLiteral("设备已关闭接单，请先启用设备。")); return; }
            device.enabled = true; setProperty(qPrintable(QStringLiteral("simulation_active_%1").arg(index)), true); simulation_->start();
        } else if (action == QLatin1String("pause")) device.enabled = false;
        else if (action == QLatin1String("toggle-device")) device.online = !device.online;
        else {
            if (!device.online || device.busy) { fail(QStringLiteral("设备离线或忙碌，无法打印测试页。")); return; }
            Order test; test.id = QStringLiteral("TEST-%1").arg(snapshot_.orders.size() + 1); test.title = QStringLiteral("设备测试页（样例）");
            test.pages = 1; test.duplex = false; test.color = device.color; test.printer = index;
            test.student = QStringLiteral("店主测试"); test.submittedMs = QDateTime(QDate(2026, 10, 3), QTime(10, 24)).toMSecsSinceEpoch();
            snapshot_.orders.prepend(test);
            device.enabled=true;setProperty(qPrintable(QStringLiteral("simulation_active_%1").arg(index)),true);simulation_->start();
        }
    } else if (action == QLatin1String("move-before")) {
        int source=-1,target=-1;
        for(int i=0;i<snapshot_.orders.size();++i){if(snapshot_.orders[i].id==fields.value(0))source=i;if(snapshot_.orders[i].id==fields.value(1))target=i;}
        if(source<0||target<0||snapshot_.orders[source].state!=QStringLiteral("待打印")||snapshot_.orders[target].state!=QStringLiteral("待打印")||snapshot_.orders[source].printer!=snapshot_.orders[target].printer){fail(QStringLiteral("仅可拖动同设备的等待任务；跨设备请使用转移设备。"));return;}
        if(source!=target){const auto moved=snapshot_.orders.takeAt(source);if(source<target)--target;snapshot_.orders.insert(target,moved);}
    } else if (action == QLatin1String("reorder") || action == QLatin1String("transfer") || action == QLatin1String("assign")) {
        int row = -1; for (int i = 0; i < snapshot_.orders.size(); ++i) if (snapshot_.orders[i].id == fields.value(0)) { row = i; break; }
        if (row < 0 || snapshot_.orders[row].state != QStringLiteral("待打印")) { fail(QStringLiteral("仅可操作待打印订单。")); return; }
        if (action == QLatin1String("reorder")) {
            const int step = fields.value(1) == QLatin1String("up") ? -1 : fields.value(1) == QLatin1String("down") ? 1 : 0;
            if (step == 0) { fail(QStringLiteral("未知排序方向。")); return; }
            int other = row + step;
            while (other >= 0 && other < snapshot_.orders.size() && (snapshot_.orders[other].state != QStringLiteral("待打印") || snapshot_.orders[other].printer != snapshot_.orders[row].printer)) other += step;
            if (other < 0 || other >= snapshot_.orders.size()) { fail(QStringLiteral("订单已在当前设备队列边缘。")); return; }
            snapshot_.orders.swapItemsAt(row, other);
        } else {
            bool validTarget = false; const int target = fields.value(1).toInt(&validTarget);
            if (!validTarget || target < 0 || target >= snapshot_.devices.size() || !compatible(snapshot_.orders[row], snapshot_.devices[target])) { fail(QStringLiteral("目标设备不支持此订单规格。")); return; }
            snapshot_.orders[row].printer = target;
        }
    } else if (action == QLatin1String("restock")) {
        bool validQuantity = false; const int quantity = fields.value(1).toInt(&validQuantity);
        if (!validQuantity || quantity < 1 || quantity > 1000000 || (fields.value(0) != QLatin1String("a4") && fields.value(0) != QLatin1String("a3"))) { fail(QStringLiteral("补纸参数无效。")); return; }
        for (auto& device : snapshot_.devices) if (fields[0] == QLatin1String("a4") || device.a3) { device.paper += quantity; device.capacity = std::max(device.capacity, device.paper); }
    } else if (action == QLatin1String("pickup") || action == QLatin1String("cancel")) {
        bool found = false;
        for (auto& order : snapshot_.orders) {
            if (order.id != value) continue;
            if (action == QLatin1String("pickup") && order.state == QStringLiteral("待取件")) { order.state = QStringLiteral("已完成"); found = true; }
            if (action == QLatin1String("cancel") && order.state == QStringLiteral("待打印")) { order.state = QStringLiteral("已取消"); found = true; }
            break;
        }
        if (!found) { fail(QStringLiteral("订单状态不允许此操作。")); return; }
    } else if (action == QLatin1String("preview")) {
        fail(QStringLiteral("只有文件名和规格元数据；样例未提供文档内容。")); return;
    } else { fail(QStringLiteral("未支持此操作：") + action); return; }
    recount(); emit changed(); emit feedback(QStringLiteral("样例状态已更新：") + action, false);
}

void MerchantData::setDevice(int index, QJsonObject config) {
    if (index < 0 || index >= snapshot_.devices.size()) { emit feedback(QStringLiteral("无效设备。"), true); return; }
    if (snapshot_.devices[index].busy) { emit feedback(QStringLiteral("请等待设备空闲后修改配置。"), true); return; }
    if (!snapshot_.showcase) {
        if (!snapshot_.connected) { emit feedback(QStringLiteral("后台未连接，配置未发送。"), true); return; }
        const auto fleet = lastState_.shop.value(QStringLiteral("fleet")).toArray();
        if (index >= fleet.size()) { emit feedback(QStringLiteral("旧版队列没有设备经营配置。"), true); return; }
        auto merged = fleet[index].toObject();
        if(config.contains(QStringLiteral("enabled")))config.insert(QStringLiteral("online"), config.take(QStringLiteral("enabled")));
        for (auto it = config.constBegin(); it != config.constEnd(); ++it) merged.insert(it.key(), it.value());
        QMetaObject::invokeMethod(controller_, [controller = controller_, index, merged] { controller->shopCommand(QStringLiteral("set_printer"), {{QStringLiteral("index"), index}, {QStringLiteral("printer"), merged}}); }, Qt::QueuedConnection);
        return;
    }
    auto& device = snapshot_.devices[index];
    device.name = config.value(QStringLiteral("name")).toString(device.name);
    device.online = config.value(QStringLiteral("online")).toBool(device.online);
    device.enabled = config.value(QStringLiteral("enabled")).toBool(device.enabled);
    device.color = config.value(QStringLiteral("color")).toBool(device.color);
    device.duplex = config.value(QStringLiteral("duplex")).toBool(device.duplex);
    device.a3 = config.value(QStringLiteral("a3")).toBool(device.a3);
    recount(); emit changed(); emit feedback(QStringLiteral("样例设备配置已更新。"), false);
}

void MerchantData::addSampleDevice(const QString& name, bool color) {
    if(!snapshot_.showcase)return;
    if(snapshot_.devices.size()>=6){emit feedback(QStringLiteral("样例最多支持六台设备。"),true);return;}
    Device device;device.index=snapshot_.devices.size();device.name=name.trimmed();device.color=color;
    if(device.name.isEmpty()){emit feedback(QStringLiteral("请输入设备名称。"),true);return;}
    snapshot_.devices.append(device);recount();emit changed();emit feedback(QStringLiteral("样例设备已添加。"),false);
}

void MerchantData::setRange(const QString& range) {
    if (range != QLatin1String("today") && range != QLatin1String("week") && range != QLatin1String("month") && !QDate::fromString(range, Qt::ISODate).isValid()) { emit feedback(QStringLiteral("未知统计时间范围。"), true); return; }
    range_ = range; recount(); emit changed();
    bool hasHistory = false; for (const auto& order : snapshot_.orders) if (order.submittedMs > 0) { hasHistory = true; break; }
    if (!hasHistory) emit feedback(QStringLiteral("后台没有可用于该范围的订单时间历史，统计为空。"), false);
    else if (snapshot_.showcase) emit feedback(QStringLiteral("样例所有订单均发生在 10 月 3 日；今日、当周、当月使用同一批订单。"), false);
    else emit feedback(QStringLiteral("已按真实提交、完成时间统计；当前队列状态仍显示全部未完成订单。"), false);
}

void MerchantData::exportReport(const QString& path) {
    if (!snapshot_.showcase) {
        emit feedback(QStringLiteral("导出完整持久历史报表，包含原始时间与经营字段。"), false);
        QMetaObject::invokeMethod(controller_, [controller = controller_, path] { controller->exportReport(path); }, Qt::QueuedConnection);
        return;
    }
    QJsonArray orders;
    for (const auto& order : snapshot_.orders) orders.append(QJsonObject{{QStringLiteral("job_id"), order.id}, {QStringLiteral("title"), order.title},
        {QStringLiteral("customer"), order.student}, {QStringLiteral("state"), order.state}, {QStringLiteral("pages"), order.pages}, {QStringLiteral("copies"), order.copies},
        {QStringLiteral("printed_sheets"), order.printed}, {QStringLiteral("assigned_printer"), order.printer}, {QStringLiteral("quoted_cents"), static_cast<double>(order.amountCents)},
        {QStringLiteral("submitted_ms"), static_cast<double>(order.submittedMs)}, {QStringLiteral("finished_ms"), static_cast<double>(order.finishedMs)}});
    const QJsonObject report{{QStringLiteral("schema_version"), 1}, {QStringLiteral("mode"), QStringLiteral("merchant_showcase")},
        {QStringLiteral("showcase"), true}, {QStringLiteral("range"), range_}, {QStringLiteral("revenue_cents"), static_cast<double>(snapshot_.revenueCents)},
        {QStringLiteral("printed_pages"), snapshot_.printedPages}, {QStringLiteral("jobs"), orders}};
    QSaveFile file(path); const auto bytes = QJsonDocument(report).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) emit feedback(QStringLiteral("样例报表写入失败：") + file.errorString(), true);
    else emit feedback(QStringLiteral("样例报表已保存：") + path, false);
}
}
