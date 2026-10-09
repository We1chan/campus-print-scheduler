#include "merchant_dispatch_model.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

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
    std::array<int, 6> priorities{{1, 9, 2, 3, 7, 6}};
    // Times here are in teaching minutes. Zero means no delivery deadline.
    std::array<int, 6> deadlineMinutes{{12, 3, 8, 5, 0, 2}};
    std::array<int, 6> arrivalMinutes{};
    if (scenario == 1) {
        deadlineMinutes = {{4, 2, 8, 5, 10, 1}};
        priorities = {{2, 5, 1, 3, 4, 6}};
    } else if (scenario == 2) {
        arrivalMinutes = {{0, 0, 1, 2, 3, 4}};
        priorities = {{6, 0, 1, 2, 1, 3}};
        deadlineMinutes = {{9, 6, 8, 7, 0, 6}};
    } else if (scenario == 3) {
        arrivalMinutes = {{0, 0, 0, 1, 1, 2}};
        priorities = {{1, 7, 2, 3, 5, 9}};
        deadlineMinutes = {{11, 3, 8, 5, 0, 4}};
    }
    std::vector<cps::Job> result;
    result.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        cps::Job job;
        job.id = ids[index];
        job.title = titles[index].toUtf8().toStdString();
        job.pages = pages[index];
        job.priority = priorities[index];
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
} // namespace

QStringList dispatchScenarioNames() {
    return {QStringLiteral("长单与短单"), QStringLiteral("临近取件期限"),
        QStringLiteral("优先级与等待老化"), QStringLiteral("营业高峰与收入")};
}

DispatchDemo makeDispatchDemo(int scenario) {
    const auto names = dispatchScenarioNames();
    if (scenario < 0 || scenario >= names.size()) scenario = 0;
    DispatchDemo demo;
    demo.scenarioName = names[scenario];
    const auto orders = coreOrders(scenario);
    const std::array<const char*, 6> colors{{"#A5CAEF", "#A6C398", "#ECD092",
        "#E4A6AB", "#BDB1E0", "#8CCBD0"}};
    cps::TimeMs lastArrival = 0;
    cps::TimeMs totalProcessing = 0;
    const std::vector<cps::Printer> fleet{{"教学打印机", false, true, true, true, CorePageMs, CorePageMs}};
    for (std::size_t index = 0; index < orders.size(); ++index) {
        auto displayed = orders[index];
        scaleJobTime(displayed, ClockScale);
        demo.orders.push_back({QString::fromStdString(displayed.id), QString::fromStdString(displayed.title),
            QColor(QString::fromLatin1(colors[index])), displayed});
        lastArrival = std::max(lastArrival, orders[index].submittedMs);
        totalProcessing += cps::processingMs(orders[index], fleet.front());
    }
    // Any work-conserving ordering finishes by last arrival + total service.
    // Share this conservative bound across all five rows, including idle time.
    const auto coreHorizon = lastArrival + totalProcessing;
    demo.horizonMs = coreHorizon * ClockScale;
    const std::array<cps::Policy, 5> policies{{cps::Policy::Fcfs, cps::Policy::ShortestJobFirst,
        cps::Policy::PriorityAging, cps::Policy::Deadline, cps::Policy::Revenue}};
    const std::array<QString, 5> labels{{QStringLiteral("先来先服务"), QStringLiteral("短任务优先"),
        QStringLiteral("优先级 + 老化"), QStringLiteral("最早截止优先"), QStringLiteral("收入优先")}};
    const std::array<QString, 5> explanations{{
        QStringLiteral("按到达时间派发；已开始的长单不会被打断。"),
        QStringLiteral("从已到达的订单中先选页数最少的，缩短短单等待。"),
        QStringLiteral("优先级越大越优先；教学时钟每等待 2 分钟增加 1 分，同分按到达时间。"),
        QStringLiteral("先处理取件期限最早的订单；无期限排在有期限之后，不保证全部准时。"),
        QStringLiteral("按单位打印时间的收入，结合临近期限和等待老化排序；整批完成后的总收入相同。")}};
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

const DispatchDemoOrder* findDispatchOrder(const DispatchDemo& demo, const std::string& id) {
    for (const auto& order : demo.orders) if (order.job.id == id) return &order;
    return nullptr;
}
} // namespace cps::gui::merchant
