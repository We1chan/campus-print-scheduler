#include "merchant_dispatch_model.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QSaveFile>
#include "cps/runtime/state_store.hpp"
#include "cps/runtime/shop_io.hpp"

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
} // namespace

int main(int argc, char** argv) {
    try {
        {
            constexpr cps::TimeMs epoch=1700000000000;
            cps::Scheduler actual;
            auto fleet=cps::defaultFleet(1000);
            for(int i=0;i<2;++i){cps::Job job;job.id="uploaded-"+std::to_string(i);job.title="real-upload.pdf";job.pages=12+i;job.submittedMs=epoch+i*1000;cps::quote(job);check(actual.submit(job),"valid backend cohort");}
            check(actual.claimFor(cps::Policy::Fcfs,epoch+5000,fleet,{0,1,2,3,4,5},{10000,3000}).has_value(),"one actual job is running");
            QJsonArray rows,devices;
            for(const auto& job:actual.snapshot())rows.append(cps::jobSnapshotToJson(job));
            for(const auto& printer:fleet)devices.append(cps::printerToJson(printer));
            QJsonObject source{{"type","snapshot"},{"shop_enabled",true},{"now_ms",double(epoch+6000)},
                {"jobs",rows},{"fleet",devices},{"supplied_a4",10000},{"supplied_a3",3000},{"aging_interval_ms",30000}};
            const auto original=QJsonDocument(source).toJson();
            const auto live=makeClassroomDispatchDemo(source);
            check(live.classroom&&live.orders.size()==2&&live.originMs==epoch,"real snapshot enters classroom comparison");
            check(live.orders[0].job.submittedMs==0&&live.orders[1].job.submittedMs==1000&&live.orders[0].job.attempts==0,
                "relative arrivals preserved and execution reset only in the counterfactual copy");
            check(QJsonDocument(source).toJson()==original&&actual.snapshot()[0].state==cps::JobState::Running,
                "counterfactual replay leaves the actual running job intact");
            check(live.orders[0].job.quotedCents==actual.snapshot()[0].quotedCents&&
                live.orders[0].job.deadlineMs==actual.snapshot()[0].deadlineMs-epoch,"locked quotes and promises preserved");
            auto changed=source;changed.insert("now_ms",double(epoch+9000));
            check(classroomDispatchFingerprint(changed)==classroomDispatchFingerprint(source),"clock refresh does not recalculate identical inputs");
            check(actual.cancel("uploaded-1",epoch+9000),"cancel the queued test order");
            rows={};for(const auto& job:actual.snapshot())rows.append(cps::jobSnapshotToJson(job));changed.insert("jobs",rows);
            check(classroomDispatchFingerprint(changed)!=classroomDispatchFingerprint(source)&&makeClassroomDispatchDemo(changed).orders.size()==1,
                "cancellations update the eligible live cohort");
        }
        check(dispatchScenarioNames().size() == 7, "four teaching and three campus business scenarios");
        for (int scenario = 4; scenario < 7; ++scenario) {
            const auto business = makeDispatchDemo(scenario);
            check(business.orders.size() == (scenario == 4 ? 150 : scenario == 5 ? 300 : 600),
                "business scenarios contain the requested complete cohort");
            check(business.business && business.fleet.size() == 6 && business.horizonMs == 28800000,
                "six production devices use eight unscaled business hours");
            const auto report = dispatchDemoReport(business);
            for (const cps::TimeMs now : {cps::TimeMs{0}, cps::TimeMs{7 * 60000}, cps::TimeMs{60 * 60000},
                    cps::TimeMs{240 * 60000}, cps::TimeMs{479 * 60000}, cps::TimeMs{480 * 60000}}) {
                const auto view = dispatchBusinessTimeline(business, now);
                check(view.startMs >= 0 && view.endMs <= business.horizonMs && view.endMs - view.startMs == 30 * 60000 &&
                    view.startMs <= now && now <= view.endMs, "three strategies share a bounded rolling thirty-minute clock");
                if (now == 60 * 60000) check(view.startMs == now, "local clock follows playback beyond initial window");
                for (std::size_t row = 0; row < view.runs.size(); ++row) {
                    std::vector<int> count(6);
                    for (const auto& d : view.runs[row]) {
                        check(d.finishedMs > now && d.startedMs < view.endMs && d.finishedMs > view.startMs,
                            "completed and distant future work do not clutter the local timeline");
                        check(++count[static_cast<std::size_t>(d.printer)] <= 3, "each printer shows at most three current or upcoming orders");
                        const auto& full = business.runs[row].result.dispatches;
                        check(std::any_of(full.begin(), full.end(), [&](const auto& original) {
                            return original.jobId == d.jobId && original.printer == d.printer && original.startedMs == d.startedMs && original.finishedMs == d.finishedMs;
                        }), "local view preserves original dispatch timestamps and device assignments");
                    }
                }
            }
            check(QJsonDocument(report).toJson() == QJsonDocument(dispatchDemoReport(business)).toJson(),
                "rolling display does not mutate complete replay or exported metrics");
            check(QJsonDocument(report).toJson() == QJsonDocument(dispatchDemoReport(makeDispatchDemo(scenario))).toJson(),
                "fixed-seed complete reports are reproducible");
            std::vector<cps::Job> input;
            for (const auto& order : business.orders) {
                input.push_back(order.job);
                auto quoted = order.job; cps::quote(quoted);
                check(cps::validTerms(order.job) && order.job.submittedMs < 14400000 &&
                    quoted.quotedCents == order.job.quotedCents && quoted.deadlineMs == order.job.deadlineMs,
                    "synthetic orders use valid production prices and service promises");
            }
            for (const auto& run : business.runs) {
                const auto native = cps::replayShop(input, run.policy, business.fleet, business.horizonMs, business.supplied);
                check(native.dispatches.size() == run.result.dispatches.size(), "all strategies use the production replay");
                std::vector<cps::TimeMs> available(6);
                std::vector<cps::TimeMs> waits;
                int express = 0, timely = 0;
                cps::Money profit = 0;
                for (std::size_t i = 0; i < run.result.dispatches.size(); ++i) {
                    const auto& d = run.result.dispatches[i];
                    const auto* order = findDispatchOrder(business, d.jobId);
                    check(order && d.printer >= 0 && d.printer < 6, "known order and device");
                    const auto& printer = business.fleet[static_cast<std::size_t>(d.printer)];
                    check(cps::compatible(order->job, printer) && d.startedMs >= order->job.submittedMs &&
                        d.startedMs >= available[static_cast<std::size_t>(d.printer)] && d.startedMs < business.horizonMs &&
                        d.finishedMs - d.startedMs == cps::processingMs(order->job, printer),
                        "real device capability, arrival, duration, closing and exclusive occupancy respected");
                    available[static_cast<std::size_t>(d.printer)] = d.finishedMs;
                    check(d.jobId == native.dispatches[i].jobId && d.printer == native.dispatches[i].printer &&
                        d.startedMs == native.dispatches[i].startedMs && d.finishedMs == native.dispatches[i].finishedMs,
                        "displayed multi-device trace exactly matches the production algorithm");
                }
                for (const auto& j : run.result.jobs) {
                    const auto* original = findDispatchOrder(business, j.id);
                    check(original && j.pages == original->job.pages && j.copies == original->job.copies &&
                        j.submittedMs == original->job.submittedMs && j.deadlineMs == original->job.deadlineMs &&
                        j.service == original->job.service && j.quotedCents == original->job.quotedCents,
                        "all strategy inputs are equal");
                    waits.push_back(j.firstStartedMs.value_or(business.horizonMs) - j.submittedMs);
                    const bool completed = j.state == cps::JobState::Completed;
                    if (completed) {
                        check(j.finishedMs && *j.finishedMs <= business.horizonMs, "no post-closing settlements");
                        profit += j.quotedCents - j.refundCents - j.costCents;
                    }
                    if (j.service == cps::ServiceTier::Express) {
                        ++express;
                        if (completed && *j.finishedMs <= j.deadlineMs) ++timely;
                    }
                }
                std::sort(waits.begin(), waits.end());
                long double total = 0; for (const auto wait : waits) total += wait;
                check(std::abs(run.quality.averageObservedWaitMs - double(total / waits.size())) < 1e-8 &&
                    run.quality.p95ObservedWaitMs == waits[(waits.size() * 95 + 99) / 100 - 1] &&
                    run.quality.expressAccepted == express && run.quality.expressOnTime == timely &&
                    run.result.business.contributionCents == profit, "business metrics reconstruct from all accepted jobs");
                std::cout << business.orders.size() << " " << run.label.toStdString() << " score=" << run.quality.overallScore
                    << " wait=" << run.averageWaitMinutes << " completed=" << run.result.business.completed << "\n";
            }
            if (argc == 2) {
                check(QDir().mkpath(QString::fromLocal8Bit(argv[1])), "create local evidence folder");
                QSaveFile file(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("simulation-%1.json").arg(business.orders.size())));
                const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
                check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(), "atomic export of full auditable evidence");
            }
        }
        for (int scenario = 0; scenario < 4; ++scenario) checkScenario(scenario);
        const auto standard = makeDispatchDemo(0);
        bool teachingReportRejected = false;
        try { dispatchDemoReport(standard); } catch (const std::invalid_argument&) { teachingReportRejected = true; }
        check(teachingReportRejected, "scaled teaching clock cannot be mislabeled as a business report");
        check(standard.runs.size() == 3 && standard.runs[2].policy == cps::Policy::Balanced,
            "only FCFS, SJF and balanced are offered");
        check(standard.runs[0].result.dispatches.front().jobId == "A" &&
            standard.runs[1].result.dispatches.front().jobId == "F", "FCFS and SJF materially diverge");
        check(standard.runs[1].averageWaitMinutes < standard.runs[0].averageWaitMinutes,
            "SJF reduces average waiting on this mixed-length batch");
        const auto deadline = makeDispatchDemo(1);
        check(deadline.runs[2].result.dispatches.front().jobId == "F" &&
            deadline.runs[2].onTime > deadline.runs[0].onTime, "balanced improves the controlled pickup scenario");
        const auto aging = makeDispatchDemo(2);
        const auto& dispatches = aging.runs[2].result.dispatches;
        check(dispatches.front().jobId == "B" && std::any_of(dispatches.begin(), dispatches.end(),
            [](const cps::Dispatch& d) { return d.jobId == "A"; }),
            "express service advances while the old flexible document also finishes");
        check(makeDispatchDemo(-1).scenarioName == standard.scenarioName &&
            makeDispatchDemo(7).scenarioName == standard.scenarioName, "invalid scenario falls back to initial scenario");
        check(findDispatchOrder(standard, "missing") == nullptr, "unknown order lookup is safe");
        std::cout << "merchant dispatch: four teaching and three business scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
