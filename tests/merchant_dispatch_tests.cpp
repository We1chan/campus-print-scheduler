#include "merchant_dispatch_model.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace cps::gui::merchant;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkScenario(int scenario) {
    const auto demo = makeDispatchDemo(scenario);
    check(demo.orders.size() == 6 && demo.horizonMs > 0, "six orders and a nonempty shared clock");
    int pages = 0;
    std::vector<cps::Job> nativeOrders;
    for (const auto& order : demo.orders) {
        pages += order.job.sheets();
        check(cps::validTerms(order.job), "display orders have valid commercial terms");
        check(order.job.state == cps::JobState::Queued && order.job.attempts == 0 &&
            order.job.runs.empty(), "input remains an independent unstarted workload");
        auto native = order.job;
        native.submittedMs /= 4;
        native.deadlineMs /= 4;
        nativeOrders.push_back(native);
    }
    check(pages == 32, "all scenarios use the same 32-page load");
    const std::vector<cps::Printer> nativeFleet{{"test", false, true, true, true, 5000, 5000}};
    const auto& baseline = demo.runs.front();
    for (const auto& run : demo.runs) {
        check(run.result.jobs.size() == 6 && run.result.dispatches.size() == 6 &&
            run.result.business.completed == 6, "all six orders finish under each real strategy");
        check(run.result.business.revenueCents == baseline.result.business.revenueCents,
            "full completion under every policy yields the same locked revenue");
        const auto native = cps::replayShop(nativeOrders, run.policy, nativeFleet, demo.horizonMs / 4);
        std::set<std::string> seen;
        cps::TimeMs previousFinish = 0;
        for (std::size_t index = 0; index < run.result.dispatches.size(); ++index) {
            const auto& dispatch = run.result.dispatches[index];
            const auto* original = findDispatchOrder(demo, dispatch.jobId);
            check(original && seen.insert(dispatch.jobId).second, "dispatches cover each workload ID once");
            check(dispatch.printer == 0 && dispatch.startedMs >= previousFinish,
                "single printer is never double booked");
            check(dispatch.startedMs >= original->job.submittedMs, "future arrivals cannot start early");
            check(dispatch.finishedMs - dispatch.startedMs == original->job.sheets() * 20000,
                "displayed service is exactly three pages per minute");
            check(dispatch.finishedMs <= demo.horizonMs, "common axis covers every completion");
            check(dispatch.jobId == native.dispatches[index].jobId &&
                dispatch.startedMs == native.dispatches[index].startedMs * 4 &&
                dispatch.finishedMs == native.dispatches[index].finishedMs * 4,
                "ordering and complete teaching timeline come from real replayShop");
            previousFinish = dispatch.finishedMs;
        }
        cps::TimeMs waits = 0;
        int onTime = 0;
        int noDeadline = 0;
        for (const auto& job : run.result.jobs) {
            const auto* original = findDispatchOrder(demo, job.id);
            check(original && job.pages == original->job.pages && job.copies == original->job.copies &&
                job.priority == original->job.priority && job.submittedMs == original->job.submittedMs &&
                job.deadlineMs == original->job.deadlineMs && job.quotedCents == original->job.quotedCents,
                "five policies receive identical arrival, pages, priorities, deadline and price");
            check(job.state == cps::JobState::Completed && job.firstStartedMs && job.finishedMs &&
                job.runs.size() == 1 && job.runs.front().stoppedMs, "completion carries real run history");
            check(job.runs.front().startedMs == *job.firstStartedMs &&
                *job.runs.front().stoppedMs == *job.finishedMs && job.lastChangedMs == *job.finishedMs,
                "all snapshot history timestamps share the displayed clock");
            const auto wait = *job.firstStartedMs - job.submittedMs;
            check(wait >= 0 && wait == job.accumulatedWaitMs, "waiting is actual started minus submitted");
            waits += wait;
            if (job.deadlineMs == 0) ++noDeadline;
            if (job.deadlineMs == 0 || *job.finishedMs <= job.deadlineMs) ++onTime;
        }
        const double average = static_cast<double>(waits) / 6 / 60000;
        check(std::abs(run.averageWaitMinutes - average) < 1e-9 &&
            std::abs(run.result.performance.averageFirstWaitMs / 60000 - average) < 1e-9,
            "displayed and core statistics match measured waiting");
        check(run.onTime == onTime && run.onTime == run.result.business.onTime + noDeadline,
            "on-time UI explicitly includes no-deadline completions");
        check(std::abs(run.result.performance.busyPrinterMs - 32 * 20000) < 1e-9,
            "busy time and common load are measured on the same clock");
    }
}
} // namespace

int main() {
    try {
        check(dispatchScenarioNames().size() == 4, "four available teaching scenarios");
        for (int scenario = 0; scenario < 4; ++scenario) checkScenario(scenario);
        const auto standard = makeDispatchDemo(0);
        check(standard.runs[0].result.dispatches.front().jobId == "A" &&
            standard.runs[1].result.dispatches.front().jobId == "F", "FCFS and SJF materially diverge");
        check(standard.runs[1].averageWaitMinutes < standard.runs[0].averageWaitMinutes,
            "SJF reduces average waiting on this mixed-length batch");
        const auto deadline = makeDispatchDemo(1);
        check(deadline.runs[3].result.dispatches.front().jobId == "F" &&
            deadline.runs[3].onTime > deadline.runs[0].onTime, "real deadline strategy improves deadline scenario");
        const auto aging = makeDispatchDemo(2);
        const auto& dispatches = aging.runs[2].result.dispatches;
        const auto lower = std::find_if(dispatches.begin(), dispatches.end(), [](const cps::Dispatch& d) { return d.jobId == "C"; });
        const auto higher = std::find_if(dispatches.begin(), dispatches.end(), [](const cps::Dispatch& d) { return d.jobId == "F"; });
        check(lower < higher, "older low-priority C advances ahead of newer higher-priority F through aging");
        check(makeDispatchDemo(-1).scenarioName == standard.scenarioName &&
            makeDispatchDemo(4).scenarioName == standard.scenarioName, "invalid scenario falls back to initial scenario");
        check(findDispatchOrder(standard, "missing") == nullptr, "unknown order lookup is safe");
        std::cout << "merchant dispatch: all four scenarios and five strategies passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
