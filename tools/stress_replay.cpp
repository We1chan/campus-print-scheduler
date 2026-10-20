#include "cps/core/shop_replay.hpp"
#include "cps/runtime/shop_io.hpp"
#include "cps/runtime/state_store.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
qint64 integer(const QJsonObject& object, const char* key, qint64 low, qint64 high) {
    const auto value = object.value(QLatin1String(key));
    const auto number = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number || number < low || number > high)
        throw std::invalid_argument(std::string("invalid field: ") + key);
    return qint64(number);
}

QJsonObject qualityJson(const cps::ShopQuality& q) {
    return {{"average_observed_wait_ms", q.averageObservedWaitMs}, {"p95_observed_wait_ms", q.p95ObservedWaitMs},
        {"express_accepted", q.expressAccepted}, {"express_on_time", q.expressOnTime},
        {"express_on_time_rate", q.expressOnTimeRate}, {"potential_contribution_cents", qint64(q.potentialContributionCents)},
        {"waiting_score", q.waitingScore}, {"contribution_score", q.contributionScore}, {"overall_score", q.overallScore}};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({"input", "Generated fixed stress input JSON", "file"});
    cli.addOption({"output", "C++ baseline audit JSON", "file"});
    cli.process(app);
    if (!cli.isSet("input") || !cli.isSet("output")) return 64;
    try {
        if (QFileInfo(cli.value("input")).absoluteFilePath() == QFileInfo(cli.value("output")).absoluteFilePath())
            throw std::invalid_argument("output must not replace input");
        QFile input(cli.value("input"));
        if (!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024 * 1024)
            throw std::invalid_argument("cannot read input");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(input.readAll(), &error);
        const auto root = document.object();
        const auto scenarios = root.value("scenarios").toArray();
        if (error.error != QJsonParseError::NoError || root.value("kind") != "campus-print-optimal-input" || scenarios.size() != 1)
            throw std::invalid_argument("expected one fixed stress scenario");
        const auto scenario = scenarios.at(0).toObject();
        const auto horizon = integer(scenario, "horizon_ms", 1, cps::MaxTimeMs);
        const auto stock = scenario.value("inventory").toObject();
        const cps::Inventory supplied{int(integer(stock, "a4", 0, 1000000000)), int(integer(stock, "a3", 0, 1000000000))};
        std::vector<cps::Printer> fleet;
        for (const auto& row : scenario.value("printers").toArray()) {
            cps::Printer printer;
            if (!cps::printerFromJson(row.toObject(), printer)) throw std::invalid_argument("invalid fleet");
            fleet.push_back(printer);
        }
        std::vector<cps::Job> orders;
        const auto matrix = scenario.value("processing_ms").toArray();
        const auto costs = scenario.value("cost_cents").toArray();
        for (const auto& row : scenario.value("jobs").toArray()) {
            const auto object = row.toObject();
            cps::Job job;
            job.id = object.value("job_id").toString().toStdString();
            job.title = object.value("title").toString().toUtf8().toStdString();
            job.pages = int(integer(object, "pages", 1, 1000));
            job.copies = int(integer(object, "copies", 1, 100));
            job.submittedMs = integer(object, "submitted_ms", 0, horizon);
            if (!cps::orderTermsFromJson(object, job)) throw std::invalid_argument("invalid order");
            cps::quote(job);
            if (job.baseCents != integer(object, "base_cents", 0, 3000000) ||
                job.quotedCents != integer(object, "quoted_cents", 0, 3000000) ||
                job.deadlineMs != integer(object, "deadline_ms", 0, cps::MaxTimeMs) ||
                cps::directCost(job, cps::Printer{}) != integer(object, "nominal_cost_cents", 0, 3000000))
                throw std::invalid_argument("generator pricing differs from C++ core");
            const auto index = qsizetype(orders.size());
            if (index >= matrix.size() || index >= costs.size()) throw std::invalid_argument("missing matrix rows");
            const auto times = matrix[index].toArray(), direct = costs[index].toArray();
            if (times.size() != qsizetype(fleet.size()) || direct.size() != qsizetype(fleet.size()))
                throw std::invalid_argument("wrong matrix columns");
            for (qsizetype m = 0; m < qsizetype(fleet.size()); ++m) {
                if (cps::compatible(job, fleet[m])) {
                    if (times[m].toInteger(-1) != cps::processingMs(job, fleet[m]) ||
                        direct[m].toInteger(-1) != cps::directCost(job, fleet[m]))
                        throw std::invalid_argument("generator duration/cost differs from C++ core");
                } else if (!times[m].isNull() || !direct[m].isNull())
                    throw std::invalid_argument("generator compatibility differs from C++ core");
            }
            orders.push_back(job);
        }
        if (matrix.size() != qsizetype(orders.size()) || costs.size() != qsizetype(orders.size()))
            throw std::invalid_argument("extra matrix rows");
        QJsonArray runs;
        for (const auto policy : {cps::Policy::Fcfs, cps::Policy::ShortestJobFirst}) {
            const auto replay = cps::replayShop(orders, policy, fleet, horizon, supplied);
            QJsonArray traces, jobs;
            for (const auto& d : replay.dispatches)
                traces.append(QJsonObject{{"job_id", QString::fromStdString(d.jobId)}, {"printer", d.printer},
                    {"started_ms", qint64(d.startedMs)}, {"finished_ms", qint64(d.finishedMs)}});
            for (const auto& job : replay.jobs) jobs.append(cps::jobSnapshotToJson(job));
            runs.append(QJsonObject{{"policy", cps::policyName(policy)}, {"dispatches", traces}, {"jobs", jobs},
                {"quality", qualityJson(cps::shopQuality(replay.jobs, 0, horizon))},
                {"business", cps::shopMetricsToJson(replay.business)}});
        }
        QSaveFile output(cli.value("output"));
        const auto bytes = QJsonDocument(QJsonObject{{"schema_version", 1}, {"kind", "campus-print-stress-baselines"}, {"runs", runs}}).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return 73;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 64;
    }
    return 0;
}
