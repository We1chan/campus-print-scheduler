#include "merchant_dispatch_model.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace {
QString serviceName(cps::ServiceTier tier) {
    switch (tier) {
    case cps::ServiceTier::Express: return QStringLiteral("express");
    case cps::ServiceTier::Standard: return QStringLiteral("standard");
    case cps::ServiceTier::Flexible: return QStringLiteral("flexible");
    }
    throw std::logic_error("unknown service tier in teaching orders");
}

QJsonObject qualityJson(const cps::ShopQuality& quality) {
    return {{"average_observed_wait_ms", quality.averageObservedWaitMs},
        {"p95_observed_wait_ms", quality.p95ObservedWaitMs},
        {"express_accepted", quality.expressAccepted},
        {"express_on_time", quality.expressOnTime},
        {"potential_contribution_cents", qint64(quality.potentialContributionCents)},
        {"waiting_score", quality.waitingScore},
        {"express_on_time_rate", quality.expressOnTimeRate},
        {"contribution_score", quality.contributionScore},
        {"overall_score", quality.overallScore}};
}

QJsonObject scenarioJson(int scenario) {
    const auto demo = cps::gui::merchant::makeDispatchDemo(scenario);
    // Only capabilities are needed for direct cost. Displayed processing times
    // come from the model's replay traces, not an invalid 20-second/page device.
    const cps::Printer printer{"教学打印机", false, true, true, true};
    QJsonArray jobs, processing, costs, baselines;
    for (const auto& order : demo.orders) {
        const auto& job = order.job;
        cps::TimeMs duration = 0;
        for (const auto& run : demo.runs) {
            const cps::Dispatch* matching = nullptr;
            for (const auto& dispatch : run.result.dispatches) {
                if (dispatch.jobId != job.id) continue;
                if (matching) throw std::logic_error("duplicate teaching dispatch");
                matching = &dispatch;
            }
            if (!matching || matching->printer != 0 || matching->finishedMs <= matching->startedMs)
                throw std::logic_error("missing or invalid teaching dispatch");
            const auto observed = matching->finishedMs - matching->startedMs;
            if (duration && duration != observed)
                throw std::logic_error("inconsistent teaching processing duration");
            duration = observed;
        }
        jobs.append(QJsonObject{{"job_id", QString::fromStdString(job.id)},
            {"title", order.title}, {"pages", job.pages}, {"copies", job.copies},
            {"submitted_ms", qint64(job.submittedMs)},
            {"paper", job.paper == cps::PaperSize::A3 ? "a3" : "a4"},
            {"color", job.color}, {"duplex", job.duplex}, {"service", serviceName(job.service)},
            {"deadline_ms", qint64(job.deadlineMs)}, {"quoted_cents", qint64(job.quotedCents)},
            {"refund_cents", 0}, {"nominal_cost_cents", qint64(cps::directCost(job, cps::Printer{}))}});
        processing.append(QJsonArray{qint64(duration)});
        costs.append(QJsonArray{qint64(cps::directCost(job, printer))});
    }
    for (const auto& run : demo.runs) {
        QJsonArray dispatches;
        for (const auto& dispatch : run.result.dispatches)
            dispatches.append(QJsonObject{{"job_id", QString::fromStdString(dispatch.jobId)},
                {"printer", dispatch.printer}, {"started_ms", qint64(dispatch.startedMs)},
                {"finished_ms", qint64(dispatch.finishedMs)}});
        if (run.key != "fcfs" && run.key != "sjf") continue;
        baselines.append(QJsonObject{{"policy", run.key}, {"dispatches", dispatches},
            {"quality", qualityJson(cps::shopQuality(run.result.jobs, 0, demo.horizonMs))}});
    }
    return {{"name", demo.scenarioName}, {"time_unit", "display_ms"},
        {"horizon_ms", qint64(demo.horizonMs)}, {"jobs", jobs},
        {"printers", QJsonArray{QJsonObject{{"name", QString::fromStdString(printer.name)},
            {"online", printer.online}, {"a3", printer.a3}, {"color", printer.color},
            {"duplex", printer.duplex}}}},
        {"inventory", QJsonObject{{"a4", 10000}, {"a3", 3000}}},
        {"processing_ms", processing}, {"cost_cents", costs}, {"baselines", baselines}};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("teaching-schedule-input"));
    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Export the current teaching orders and baseline replays for exact scheduling."));
    cli.addHelpOption();
    cli.addOption({"output", "Atomically write UTF-8 JSON to a file; omit for stdout", "file"});
    cli.process(app);
    if (!cli.positionalArguments().isEmpty() || (cli.isSet("output") && cli.value("output").trimmed().isEmpty())) {
        std::cerr << "expected only an optional --output <file>\n";
        return 64;
    }
    try {
        QJsonArray scenarios;
        const auto names = cps::gui::merchant::dispatchScenarioNames();
        // The separate 100-order stress report uses native clocks and its own
        // generator. This export remains the four small teaching fixtures.
        for (int scenario = 0; scenario < std::min(4, int(names.size())); ++scenario)
            scenarios.append(scenarioJson(scenario));
        const QJsonObject report{{"schema_version", 1}, {"kind", "campus-print-optimal-input"},
            {"objective", QJsonObject{{"name", "balanced-shop-quality-v1"},
                {"wait_reference_ms", 1800000}, {"waiting_weight", 1},
                {"express_weight", 1}, {"contribution_weight", 1}}},
            {"scenarios", scenarios}};
        const auto bytes = QJsonDocument(report).toJson();
        if (cli.isSet("output")) {
            QSaveFile output(cli.value("output"));
            if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
                std::cerr << "cannot write output: " << output.errorString().toStdString() << '\n';
                return 73;
            }
        } else if (std::fwrite(bytes.constData(), 1, std::size_t(bytes.size()), stdout) != std::size_t(bytes.size()) ||
                   std::fflush(stdout) != 0) {
            std::cerr << "cannot write JSON to stdout\n";
            return 73;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 64;
    }
    return 0;
}
