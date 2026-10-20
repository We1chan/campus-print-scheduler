#include "merchant_dispatch_model.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <random>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include "cps/runtime/shop_io.hpp"
#include "cps/runtime/state_store.hpp"

namespace cps::gui::merchant {
namespace {
constexpr cps::TimeMs Minute = 60000;
constexpr cps::TimeMs ClockScale = 4;
constexpr cps::TimeMs CorePageMs = 5000;

void scaleJobTime(cps::Job& job, cps::TimeMs factor) {
    job.submittedMs *= factor;
    job.deadlineMs *= factor;
    job.queuedSinceMs *= factor;
    job.accumulatedWaitMs *= factor;
    job.lastChangedMs *= factor;
    if (job.firstStartedMs) *job.firstStartedMs *= factor;
    if (job.finishedMs) *job.finishedMs *= factor;
    for (auto& interval : job.runs) {
        interval.startedMs *= factor;
        if (interval.stoppedMs) *interval.stoppedMs *= factor;
    }
}

std::vector<cps::Job> coreOrders(int scenario) {
    const std::array<const char*, 6> ids{{"A", "B", "C", "D", "E", "F"}};
    const std::array<QString, 6> titles{{QStringLiteral("报告"), QStringLiteral("作业"),
        QStringLiteral("讲义"), QStringLiteral("表格"), QStringLiteral("材料"), QStringLiteral("简历")}};
    const std::array<int, 6> pages{{12, 2, 6, 3, 8, 1}};
    // Times here are in teaching minutes. Zero means no delivery deadline.
    std::array<int, 6> deadlineMinutes{{12, 3, 8, 5, 0, 2}};
    std::array<int, 6> arrivalMinutes{};
    if (scenario == 1) {
        deadlineMinutes = {{4, 2, 8, 5, 10, 1}};
    } else if (scenario == 2) {
        arrivalMinutes = {{0, 0, 1, 2, 3, 4}};
        deadlineMinutes = {{9, 6, 8, 7, 0, 6}};
    } else if (scenario == 3) {
        arrivalMinutes = {{0, 0, 0, 1, 1, 2}};
        deadlineMinutes = {{11, 3, 8, 5, 0, 4}};
    }
    std::vector<cps::Job> result;
    result.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        cps::Job job;
        job.id = ids[index];
        job.title = titles[index].toUtf8().toStdString();
        job.pages = pages[index];
        job.submittedMs = arrivalMinutes[index] * Minute / ClockScale;
        // Both print modes have the same demo speed; prices use the real quote.
        job.color = index == 3 || index == 5 || (scenario == 3 && index == 4);
        job.service = (index == 0 || index == 4) ? cps::ServiceTier::Flexible :
            (index == 2 ? cps::ServiceTier::Standard : cps::ServiceTier::Express);
        cps::quote(job);
        // Teaching pickup promises deliberately replace quote's default hours.
        job.deadlineMs = deadlineMinutes[index] * Minute / ClockScale;
        result.push_back(std::move(job));
    }
    return result;
}

void runBusinessComparison(DispatchDemo& demo, const std::vector<cps::Job>& orders) {
    const std::array<cps::Policy, 3> policies{{cps::Policy::Fcfs, cps::Policy::ShortestJobFirst, cps::Policy::Balanced}};
    const std::array<QString, 3> labels{{QStringLiteral("FCFS · 先来先服务"), QStringLiteral("SJF · 短任务优先"), QStringLiteral("综合调度")}};
    const std::array<QString, 3> explanations{{QStringLiteral("按到达顺序排队，选择编号最小的空闲兼容设备。"),
        QStringLiteral("按打印面数从少到多排队，选择最快的空闲兼容设备。"),
        QStringLiteral("复用内置加权响应比：服务、等待、期限和收益；分配时保留稀缺设备，再比较速度、成本与能力。")}};
    for (std::size_t i = 0; i < policies.size(); ++i) {
        auto& run = demo.runs[i];
        run.policy = policies[i]; run.label = labels[i];
        run.explanation = explanations[i];
        run.result = cps::replayShop(orders, run.policy, demo.fleet, demo.horizonMs, demo.supplied);
        run.quality = cps::shopQuality(run.result.jobs, 0, demo.horizonMs);
        run.averageWaitMinutes = run.quality.averageObservedWaitMs / Minute;
        run.onTime = run.result.business.onTime;
    }
}

DispatchDemo businessDemo(int scenario) {
    DispatchDemo demo;
    demo.business = true;
    demo.seed = 20261018;
    demo.scenarioName = dispatchScenarioNames()[scenario];
    demo.horizonMs = 8 * 60 * Minute;
    demo.fleet = cps::defaultFleet(1250);
    const int count = scenario == 4 ? 150 : scenario == 5 ? 300 : 600;
    std::mt19937 random(demo.seed);
    const std::array<const char*, 6> colors{{"#A5CAEF", "#A6C398", "#ECD092", "#E4A6AB", "#BDB1E0", "#8CCBD0"}};
    const std::array<QString, 6> titles{{QStringLiteral("课程作业"), QStringLiteral("实验报告"),
        QStringLiteral("复习讲义"), QStringLiteral("社团表格"), QStringLiteral("彩色材料"), QStringLiteral("A3 设计图")}};
    const std::array<int, 6> low{{2, 12, 40, 1, 16, 2}}, high{{16, 90, 200, 8, 90, 30}}, copies{{2, 3, 6, 15, 3, 3}};
    std::vector<cps::Job> orders;
    for (int i = 0; i < count; ++i) {
        const auto profile = static_cast<std::size_t>(random() % 6);
        cps::Job job;
        job.id = QStringLiteral("P%1").arg(i + 1, 3, 10, QChar('0')).toStdString();
        job.title = titles[profile].toUtf8().toStdString();
        job.pages = low[profile] + int(random() % (high[profile] - low[profile] + 1));
        job.copies = 1 + int(random() % copies[profile]);
        job.submittedMs = random() % (4 * 60 * Minute);
        job.paper = profile == 5 ? cps::PaperSize::A3 : cps::PaperSize::A4;
        job.color = profile >= 4;
        job.duplex = profile != 3 && random() % 2 != 0;
        const auto tier = random() % 100;
        job.service = tier < 35 ? cps::ServiceTier::Express : tier < 75 ? cps::ServiceTier::Standard : cps::ServiceTier::Flexible;
        cps::quote(job);
        orders.push_back(job);
        demo.orders.push_back({QString::fromStdString(job.id), titles[profile], QColor(colors[profile]), job});
    }
    // Sort only the shared presentation/input order; preserve the generated IDs.
    std::stable_sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) { return a.submittedMs < b.submittedMs; });
    std::stable_sort(demo.orders.begin(), demo.orders.end(), [](const auto& a, const auto& b) { return a.job.submittedMs < b.job.submittedMs; });
    runBusinessComparison(demo, orders);
    return demo;
}
} // namespace

QStringList dispatchScenarioNames() {
    return {QStringLiteral("长单与短单"), QStringLiteral("临近取件期限"),
        QStringLiteral("服务档位与等待老化"), QStringLiteral("营业高峰与收益"),
        QStringLiteral("校园经营 · 150 单"), QStringLiteral("校园经营 · 300 单"), QStringLiteral("校园高峰 · 600 单")};
}

DispatchDemo makeDispatchDemo(int scenario) {
    const auto names = dispatchScenarioNames();
    if (scenario < 0 || scenario >= names.size()) scenario = 0;
    if (scenario >= 4) return businessDemo(scenario);
    DispatchDemo demo;
    demo.scenarioName = names[scenario];
    const auto orders = coreOrders(scenario);
    const std::array<const char*, 6> colors{{"#A5CAEF", "#A6C398", "#ECD092",
        "#E4A6AB", "#BDB1E0", "#8CCBD0"}};
    cps::TimeMs lastArrival = 0;
    cps::TimeMs totalProcessing = 0;
    const std::vector<cps::Printer> fleet{{"教学打印机", false, true, true, true, CorePageMs, CorePageMs}};
    demo.fleet = fleet;
    demo.fleet.front().monoPageMs *= ClockScale;
    demo.fleet.front().colorPageMs *= ClockScale;
    for (std::size_t index = 0; index < orders.size(); ++index) {
        auto displayed = orders[index];
        scaleJobTime(displayed, ClockScale);
        demo.orders.push_back({QString::fromStdString(displayed.id), QString::fromStdString(displayed.title),
            QColor(QString::fromLatin1(colors[index])), displayed});
        lastArrival = std::max(lastArrival, orders[index].submittedMs);
        totalProcessing += cps::processingMs(orders[index], fleet.front());
    }
    // Any work-conserving ordering finishes by last arrival + total service.
    // Share this conservative bound across all three rows, including idle time.
    const auto coreHorizon = lastArrival + totalProcessing;
    demo.horizonMs = coreHorizon * ClockScale;
    const std::array<cps::Policy, 3> policies{{cps::Policy::Fcfs, cps::Policy::ShortestJobFirst, cps::Policy::Balanced}};
    const std::array<QString, 3> labels{{QStringLiteral("FCFS · 先来先服务"), QStringLiteral("SJF · 短任务优先"), QStringLiteral("综合调度")}};
    const std::array<QString, 3> explanations{{
        QStringLiteral("队列按到达顺序，依次分配空闲兼容机；已开始的订单继续打印。"),
        QStringLiteral("队列按打印面数从少到多，分配最快兼容机，缩短短单等待。"),
        QStringLiteral("队列兼顾档位、等待、期限与贡献收益；分配时保留稀缺设备，再比较速度和成本。")}};
    for (std::size_t index = 0; index < demo.runs.size(); ++index) {
        auto& run = demo.runs[index];
        run.label = labels[index];
        run.explanation = explanations[index] + QStringLiteral(" 无取件期限的完成订单计为按时。教学时钟：核心 5 秒/页 × 4，即 3 页/分钟。");
        run.policy = policies[index];
        run.result = cps::replayShop(orders, run.policy, fleet, coreHorizon);
        for (auto& job : run.result.jobs) scaleJobTime(job, ClockScale);
        for (auto& dispatch : run.result.dispatches) {
            dispatch.startedMs *= ClockScale;
            dispatch.finishedMs *= ClockScale;
        }
        run.result.performance = cps::statistics(run.result.jobs, 0, demo.horizonMs, 1);
        run.result.business = cps::shopMetrics(run.result.jobs, 0, demo.horizonMs);
        run.quality = cps::shopQuality(run.result.jobs, 0, demo.horizonMs);
        cps::TimeMs totalWait = 0;
        int started = 0;
        for (const auto& job : run.result.jobs) {
            if (job.firstStartedMs) {
                totalWait += *job.firstStartedMs - job.submittedMs;
                ++started;
            }
            if (job.state == cps::JobState::Completed && job.finishedMs &&
                (job.deadlineMs == 0 || *job.finishedMs <= job.deadlineMs)) ++run.onTime;
        }
        if (started > 0) run.averageWaitMinutes = static_cast<double>(totalWait) / started / Minute;
    }
    return demo;
}

DispatchTimeline dispatchBusinessTimeline(const DispatchDemo& demo, cps::TimeMs nowMs) {
    if (!demo.business) throw std::invalid_argument("rolling timeline requires a business scenario");
    DispatchTimeline view;
    const auto now = std::clamp<cps::TimeMs>(nowMs, 0, demo.horizonMs);
    const auto span = std::min<cps::TimeMs>(30 * Minute, demo.horizonMs);
    // Move in five-minute steps so axis labels remain readable during playback.
    view.startMs = std::min((now / (5 * Minute)) * (5 * Minute), demo.horizonMs - span);
    view.endMs = view.startMs + span;
    for (std::size_t row = 0; row < demo.runs.size(); ++row) {
        std::vector<int> count(demo.fleet.size());
        for (const auto& dispatch : demo.runs[row].result.dispatches) {
            if (dispatch.finishedMs <= now || dispatch.startedMs >= view.endMs || dispatch.finishedMs <= view.startMs) continue;
            const auto printer = static_cast<std::size_t>(dispatch.printer);
            if (printer >= count.size() || count[printer] >= 3) continue;
            view.runs[row].push_back(dispatch);
            ++count[printer];
        }
    }
    return view;
}

QByteArray classroomDispatchFingerprint(const QJsonObject& snapshot) {
    QJsonArray rows;
    for (const auto& value : snapshot.value("jobs").toArray()) {
        const auto original = value.toObject();
        const auto state = original.value("state").toString();
        if (state == "cancelled" || state == "failed" || state == "cancelling" || original.value("refund_cents").toDouble() > 0) continue;
        QJsonObject row;
        for (const auto* key : {"job_id","title","pages","copies","submitted_ms","paper","color","duplex","service","base_cents","quoted_cents","deadline_ms","payment","customer"}) row.insert(key, original.value(key));
        rows.append(row);
    }
    return QCryptographicHash::hash(QJsonDocument(QJsonObject{{"jobs", rows}, {"fleet", snapshot.value("fleet")},
        {"supplied_a4", snapshot.value("supplied_a4")}, {"supplied_a3", snapshot.value("supplied_a3")},
        {"aging_interval_ms", snapshot.value("aging_interval_ms")}}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}

DispatchDemo makeClassroomDispatchDemo(const QJsonObject& snapshot) {
    if (snapshot.value("type") != "snapshot" || !snapshot.value("shop_enabled").toBool() || !snapshot.value("jobs").isArray())
        throw std::invalid_argument("请先连接同一数据目录的经营模式后台");
    if (snapshot.value("aging_interval_ms").toDouble(30000) != 30000)
        throw std::invalid_argument("当前同批回放只支持默认的 30 秒老化间隔");
    const double clock=snapshot.value("now_ms").toDouble(-1);
    if(!std::isfinite(clock)||clock<0||clock>cps::MaxTimeMs||std::floor(clock)!=clock)throw std::invalid_argument("后台时钟无效");
    DispatchDemo demo;
    demo.business = true; demo.classroom = true;
    demo.scenarioName = QStringLiteral("现场订单 · 同批实时对比");
    demo.sourceSnapshot = snapshot;
    demo.supplied = {snapshot.value("supplied_a4").toInt(-1), snapshot.value("supplied_a3").toInt(-1)};
    if (demo.supplied.a4 < 0 || demo.supplied.a3 < 0) throw std::invalid_argument("后台纸张供应无效");
    for (const auto& value : snapshot.value("fleet").toArray()) {
        cps::Printer printer;
        if (!value.isObject() || !cps::printerFromJson(value.toObject(), printer)) throw std::invalid_argument("后台设备配置无效");
        demo.fleet.push_back(printer);
    }
    if (demo.fleet.empty() || demo.fleet.size() > 6) throw std::invalid_argument("当前工作台支持 1–6 台设备");
    std::vector<cps::Job> orders;
    demo.originMs = static_cast<cps::TimeMs>(snapshot.value("now_ms").toDouble());
    for (const auto& value : snapshot.value("jobs").toArray()) {
        cps::Job job; QString error;
        if (!value.isObject() || !cps::jobSnapshotFromJson(value.toObject(), job, error)) throw std::invalid_argument("后台订单快照无效");
        if(job.submittedMs>demo.originMs && job.submittedMs>static_cast<cps::TimeMs>(clock))throw std::invalid_argument("后台订单到达时间晚于快照");
        if (job.state == cps::JobState::Cancelled || job.state == cps::JobState::Failed || job.state == cps::JobState::Cancelling || job.refundCents > 0) continue;
        demo.originMs = std::min(demo.originMs, job.submittedMs);
        // Counterfactual cohort replay, never a rewrite of the actual queue.
        job.state = cps::JobState::Queued; job.attempts = 0; job.costCents = 0; job.refundCents = 0;
        job.assignedPrinter = -1; job.accumulatedWaitMs = 0; job.queuedSinceMs = 0; job.lastChangedMs = 0;
        job.firstStartedMs.reset(); job.finishedMs.reset(); job.runs.clear();
        orders.push_back(job);
    }
    if (orders.size() > 1000) throw std::invalid_argument("现场对比最多接入 1000 单，请使用新的课堂数据目录");
    demo.horizonMs = 8 * 60 * Minute;
    const std::array<const char*, 6> colors{{"#A5CAEF", "#A6C398", "#ECD092", "#E4A6AB", "#BDB1E0", "#8CCBD0"}};
    for (auto& job : orders) {
        job.submittedMs -= demo.originMs;
        if (job.deadlineMs) job.deadlineMs -= demo.originMs;
        demo.horizonMs = std::max(demo.horizonMs, job.submittedMs + 8 * 60 * Minute);
        const int index = int(demo.orders.size());
        demo.orders.push_back({QString::fromStdString(job.id), QString::fromUtf8(job.title.data(), qsizetype(job.title.size())),
            QColor(colors[static_cast<std::size_t>(index % 6)]), job, QStringLiteral("S%1").arg(index + 1, 3, 10, QChar('0'))});
    }
    runBusinessComparison(demo, orders);
    return demo;
}

QString dispatchBusinessComparison(const DispatchDemo& demo) {
    const auto& balanced = demo.runs[2];
    QStringList rows;
    for (int i = 0; i < 2; ++i) {
        const auto& base = demo.runs[static_cast<std::size_t>(i)];
        rows << QStringLiteral("对比 %1：综合分 %2%3，平均等待 %4%5 分钟，快速准时率 %6%7 个百分点，贡献利润 %8%9 元")
            .arg(i == 0 ? QStringLiteral("FCFS") : QStringLiteral("SJF"))
            .arg(balanced.quality.overallScore >= base.quality.overallScore ? "+" : "")
            .arg(balanced.quality.overallScore - base.quality.overallScore, 0, 'f', 2)
            .arg(balanced.averageWaitMinutes >= base.averageWaitMinutes ? "+" : "")
            .arg(balanced.averageWaitMinutes - base.averageWaitMinutes, 0, 'f', 1)
            .arg(balanced.quality.expressOnTimeRate >= base.quality.expressOnTimeRate ? "+" : "")
            .arg(100 * (balanced.quality.expressOnTimeRate - base.quality.expressOnTimeRate), 0, 'f', 1)
            .arg(balanced.result.business.contributionCents >= base.result.business.contributionCents ? "+" : "")
            .arg((balanced.result.business.contributionCents - base.result.business.contributionCents) / 100., 0, 'f', 2);
    }
    return rows.join('\n');
}

QJsonObject dispatchDemoReport(const DispatchDemo& demo) {
    if (!demo.business) throw std::invalid_argument("business report requires an unscaled business scenario");
    QJsonArray inputs, fleet, runs;
    for (const auto& order : demo.orders) {auto row = cps::jobSnapshotToJson(order.job);if (!order.label.isEmpty()) row.insert("display_id", order.label);inputs.append(row);}
    for (const auto& printer : demo.fleet) fleet.append(cps::printerToJson(printer));
    for (const auto& run : demo.runs) {
        QJsonArray jobs, dispatches;
        for (const auto& job : run.result.jobs) jobs.append(cps::jobSnapshotToJson(job));
        for (const auto& d : run.result.dispatches) dispatches.append(QJsonObject{{"job_id", QString::fromStdString(d.jobId)},
            {"printer", d.printer}, {"started_ms", double(d.startedMs)}, {"finished_ms", double(d.finishedMs)}});
        const auto& q = run.quality;
        runs.append(QJsonObject{{"policy", cps::policyName(run.policy)}, {"jobs", jobs}, {"dispatches", dispatches},
            {"accepted", int(demo.orders.size())}, {"completed", run.result.business.completed},
            {"revenue_cents", double(run.result.business.revenueCents)}, {"contribution_cents", double(run.result.business.contributionCents)},
            {"business", cps::shopMetricsToJson(run.result.business)}, {"average_observed_wait_ms", q.averageObservedWaitMs},
            {"p95_observed_wait_ms", q.p95ObservedWaitMs}, {"express_accepted", q.expressAccepted}, {"express_on_time", q.expressOnTime},
            {"express_on_time_rate", q.expressOnTimeRate}, {"potential_contribution_cents", double(q.potentialContributionCents)},
            {"waiting_score", q.waitingScore}, {"contribution_score", q.contributionScore}, {"quality_score", q.overallScore}});
    }
    return {{"schema_version", 1}, {"kind", "campus-business-simulation"}, {"generator", demo.classroom ? "classroom-snapshot-v1" : "campus-profiles-v1"},
        {"source", demo.classroom ? QStringLiteral("后台有效订单的同批对照重放；非实际进度或当前队列预测") : QStringLiteral("合成校园业务订单；非真实客户记录")}, {"seed", double(demo.seed)},
        {"origin_ms", double(demo.originMs)}, {"source_snapshot", demo.sourceSnapshot},
        {"scenario", demo.scenarioName}, {"horizon_ms", double(demo.horizonMs)}, {"clock_scale", demo.business ? 1 : 4},
        {"supplied_a4", double(demo.supplied.a4)}, {"supplied_a3", double(demo.supplied.a3)},
        {"evaluation", "balanced-shop-quality-v1"}, {"wait_reference_ms", 1800000},
        {"score_weights", QJsonArray{1, 1, 1}}, {"inputs", inputs}, {"fleet", fleet}, {"runs", runs},
        {"input_sha256", QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(inputs).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex())}};
}

const DispatchDemoOrder* findDispatchOrder(const DispatchDemo& demo, const std::string& id) {
    for (const auto& order : demo.orders) if (order.job.id == id) return &order;
    return nullptr;
}
} // namespace cps::gui::merchant
