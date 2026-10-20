#include "merchant_dispatch_model.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
        const auto native = run.key == "optimal" ? cps::ShopReplayResult{} :
            cps::replayShop(nativeOrders, run.policy, nativeFleet, demo.horizonMs / 4);
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
            check(run.key == "optimal" || (dispatch.jobId == native.dispatches[index].jobId &&
                dispatch.startedMs == native.dispatches[index].startedMs * 4 &&
                dispatch.finishedMs == native.dispatches[index].finishedMs * 4),
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
                "three policies receive identical arrival, pages, service, deadline and price");
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

QJsonObject qualityJson(const cps::ShopReplayResult& result, cps::TimeMs horizon) {
    const auto q = cps::shopQuality(result.jobs, 0, horizon);
    return {{"average_observed_wait_ms", q.averageObservedWaitMs}, {"p95_observed_wait_ms", q.p95ObservedWaitMs},
        {"express_accepted", q.expressAccepted}, {"express_on_time", q.expressOnTime},
        {"express_on_time_rate", q.expressOnTimeRate}, {"potential_contribution_cents", qint64(q.potentialContributionCents)},
        {"contribution_cents", qint64(result.business.contributionCents)}, {"waiting_score", q.waitingScore},
        {"contribution_score", q.contributionScore}, {"overall_score", q.overallScore},
        {"completed", result.business.completed}};
}

QJsonObject comparisonFixture() {
    constexpr cps::TimeMs horizon = 2000;
    const cps::Printer printer{"fixture", false, false, true, true, 10, 10};
    std::vector<cps::Job> orders;
    QJsonArray jobs, durations, costs;
    for (int i = 0; i < 100; ++i) {
        cps::Job job; job.id = "J" + std::to_string(i + 1); job.title = job.id;
        job.service = cps::ServiceTier::Express; cps::quote(job); job.deadlineMs = 1000;
        orders.push_back(job);
        jobs.append(QJsonObject{{"job_id", QString::fromStdString(job.id)}, {"title", QString::fromStdString(job.title)},
            {"pages", 1}, {"copies", 1}, {"submitted_ms", 0}, {"paper", "a4"}, {"color", false},
            {"duplex", false}, {"service", "express"}, {"deadline_ms", 1000}, {"base_cents", 20},
            {"quoted_cents", 20}, {"refund_cents", 0}, {"nominal_cost_cents", 8}});
        durations.append(QJsonArray{10}); costs.append(QJsonArray{8});
    }
    const QJsonObject scenario{{"name", "100 fixture"}, {"horizon_ms", horizon}, {"jobs", jobs},
        {"printers", QJsonArray{QJsonObject{{"name", "fixture"}, {"online", true}, {"a3", false},
            {"color", false}, {"duplex", true}, {"mono_page_ms", 10}, {"color_page_ms", 10}}}},
        {"inventory", QJsonObject{{"a4", 100}, {"a3", 0}}}, {"processing_ms", durations}, {"cost_cents", costs}};
    const auto bytes = QJsonDocument(scenario).toJson(QJsonDocument::Compact);
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    QJsonArray runs;
    const auto result = cps::replayShop(orders, cps::Policy::Fcfs, {printer}, horizon, {100, 0});
    QJsonArray dispatches;
    for (const auto& item : result.dispatches) dispatches.append(QJsonObject{{"job_id", QString::fromStdString(item.jobId)},
        {"printer", item.printer}, {"started_ms", item.startedMs}, {"finished_ms", item.finishedMs}});
    const qint64 a = 1800495, b = 1800940, reference = 1800000;
    const auto fraction = QString::number(100 * (4*a*b + reference*b + reference*a)) + "/" + QString::number(6*a*b);
    for (const auto* key : {"fcfs", "sjf", "optimal"}) runs.append(QJsonObject{{"policy", key},
        {"status", QString::fromLatin1(key) == "optimal" ? "OPTIMAL" : "BASELINE"}, {"dispatches", dispatches},
        {"quality", qualityJson(result, horizon)}, {"proof", QJsonObject{{"complete", true}, {"lower_bound_fraction", fraction},
            {"upper_bound_fraction", fraction}, {"absolute_gap_fraction", "0"}}}});
    return {{"schema_version", 1}, {"kind", "campus-print-stress-comparison"}, {"scenario", scenario},
        {"scenario_json", QString::fromUtf8(bytes)}, {"scenario_sha256", hash}, {"runs", runs}};
}

void reportTests() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary report directory");
    const auto path = temporary.filePath("report.json");
    const auto write = [&path](const QJsonObject& document) { QFile file(path); check(file.open(QIODevice::WriteOnly), "write fixture");
        const auto bytes = QJsonDocument(document).toJson(); check(file.write(bytes) == bytes.size(), "complete fixture write"); };
    const auto valid = comparisonFixture(); write(valid);
    const auto loaded = loadDispatchReport(path);
    check(loaded.orders.size() == 100 && loaded.runs[2].provenOptimal, "validated fixed 100-order report is visible");
    check(loaded.runs[0].key == "fcfs" && loaded.runs[1].key == "sjf" && loaded.runs[2].key == "optimal",
        "only the requested three offline comparisons are exposed");
    check(loaded.runs[2].result.dispatches.size() == 100 && loaded.runs[2].result.business.completed == 100,
        "loaded histories include every accepted order");
    auto incomplete = valid; auto incompleteRuns = incomplete["runs"].toArray();
    auto incompleteRun = incompleteRuns[2].toObject(); auto incompleteProof = incompleteRun["proof"].toObject();
    incompleteProof["complete"] = false; incompleteRun["proof"] = incompleteProof;
    incompleteRuns[2] = incompleteRun; incomplete["runs"] = incompleteRuns; write(incomplete);
    check(!loadDispatchReport(path).runs[2].provenOptimal,
        "incomplete proof cannot claim optimality even when status and bounds are mislabeled");
    auto wrongHash = valid; wrongHash["scenario_sha256"] = "wrong"; write(wrongHash);
    check(loadDispatchReport(path).orders.isEmpty(), "incorrect input hash refuses the report");
    auto overlap = valid; auto runs = overlap["runs"].toArray(); auto run = runs[2].toObject();
    auto trace = run["dispatches"].toArray(); auto second = trace[1].toObject(); second["started_ms"] = 0;
    second["finished_ms"] = 10; trace[1] = second; run["dispatches"] = trace; runs[2] = run; overlap["runs"] = runs; write(overlap);
    check(loadDispatchReport(path).orders.isEmpty(), "overlapping printer assignments refuse the report");
    auto incorrectQuality = valid; runs = incorrectQuality["runs"].toArray(); run = runs[2].toObject();
    auto quality = run["quality"].toObject(); quality["overall_score"] = 100; run["quality"] = quality;
    runs[2] = run; incorrectQuality["runs"] = runs; write(incorrectQuality);
    check(loadDispatchReport(path).orders.isEmpty(), "reported metrics are independently recomputed");
    auto feasible = valid; runs = feasible["runs"].toArray(); run = runs[2].toObject(); run["status"] = "FEASIBLE";
    run["proof"] = QJsonObject{}; runs[2] = run; feasible["runs"] = runs; write(feasible);
    const auto unproved = loadDispatchReport(path);
    check(unproved.orders.size() == 100 && !unproved.runs[2].provenOptimal && !unproved.runs[2].label.contains(QStringLiteral("已证明")),
        "feasible schedules remain visible without an optimum claim");
    check(loadDispatchReport(temporary.filePath("missing.json")).orders.isEmpty(), "missing report displays no fabricated orders");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--report") {
            const auto external = loadDispatchReport(QString::fromLocal8Bit(argv[2]));
            if (external.orders.size() != 100) throw std::runtime_error(external.notice.toUtf8().toStdString());
            std::cout << "external 100-order report validated; proven=" << external.runs[2].provenOptimal << '\n';
        } else check(argc == 1, "expected optional --report <path>");
        check(dispatchScenarioNames().size() == 5, "four teaching scenarios and a 100-order report");
        for (int scenario = 0; scenario < 4; ++scenario) checkScenario(scenario);
        const auto standard = makeDispatchDemo(0);
        check(standard.runs.size() == 3 && standard.runs[2].key == "optimal" && standard.runs[2].provenOptimal,
            "only FCFS, SJF and the fixed proven optimum are offered");
        check(standard.runs[0].result.dispatches.front().jobId == "A" &&
            standard.runs[1].result.dispatches.front().jobId == "F", "FCFS and SJF materially diverge");
        check(standard.runs[1].averageWaitMinutes < standard.runs[0].averageWaitMinutes,
            "SJF reduces average waiting on this mixed-length batch");
        const auto deadline = makeDispatchDemo(1);
        check(deadline.runs[2].result.dispatches.front().jobId == "F" &&
            deadline.runs[2].onTime > deadline.runs[0].onTime, "exact optimum improves the controlled pickup scenario");
        const auto aging = makeDispatchDemo(2);
        const auto& dispatches = aging.runs[2].result.dispatches;
        check(dispatches.front().jobId == "B" && std::any_of(dispatches.begin(), dispatches.end(),
            [](const cps::Dispatch& d) { return d.jobId == "A"; }),
            "express service advances while the old flexible document also finishes");
        check(makeDispatchDemo(-1).scenarioName == standard.scenarioName &&
            makeDispatchDemo(5).scenarioName == standard.scenarioName, "invalid scenario falls back to initial scenario");
        check(findDispatchOrder(standard, "missing") == nullptr, "unknown order lookup is safe");
        reportTests();
        std::cout << "merchant dispatch: teaching optima and validated 100-order reports passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
