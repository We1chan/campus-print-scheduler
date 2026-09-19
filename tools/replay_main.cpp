#include "cps/core/replay.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

namespace {
cps::TimeMs number(const QJsonObject& object, const char* key, cps::TimeMs low, cps::TimeMs high) {
    const auto value = object.value(QLatin1String(key));
    const auto n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n < static_cast<double>(low) ||
        n > static_cast<double>(high) || std::floor(n) != n)
        throw std::invalid_argument(std::string("invalid integer field: ") + key);
    return static_cast<cps::TimeMs>(n);
}
cps::TimeMs argument(const QCommandLineParser& parser, const char* key, cps::TimeMs low, cps::TimeMs high) {
    bool ok = false;
    const auto n = parser.value(QLatin1String(key)).toLongLong(&ok);
    if (!ok || n < low || n > high) throw std::invalid_argument(std::string("invalid option: ") + key);
    return n;
}
QJsonObject metrics(const cps::Statistics& s) {
    return {{"submitted", static_cast<double>(s.submitted)},
        {"first_started", static_cast<double>(s.firstStarted)},
        {"completed", static_cast<double>(s.completed)}, {"failed", static_cast<double>(s.failed)},
        {"cancelled", static_cast<double>(s.cancelled)}, {"queued", static_cast<double>(s.queued)},
        {"running", static_cast<double>(s.running)}, {"cancelling", static_cast<double>(s.cancelling)},
        {"average_first_wait_ms", s.averageFirstWaitMs},
        {"max_first_wait_ms", static_cast<double>(s.maxFirstWaitMs)},
        {"average_total_queue_wait_ms", s.averageTotalQueueWaitMs},
        {"average_turnaround_ms", s.averageTurnaroundMs},
        {"max_current_queue_wait_ms", static_cast<double>(s.maxCurrentQueueWaitMs)},
        {"busy_printer_ms", s.busyPrinterMs}, {"throughput_per_second", s.throughputPerSecond},
        {"utilization", s.utilization}};
}
QJsonObject encode(const cps::ReplayResult& result, const QString& policy) {
    QJsonArray jobs, dispatches;
    for (const auto& job : result.jobs) {
        jobs.append(QJsonObject{{"job_id", QString::fromStdString(job.id)},
            {"submitted_ms", static_cast<double>(job.submittedMs)},
            {"first_started_ms", static_cast<double>(*job.firstStartedMs)},
            {"finished_ms", static_cast<double>(*job.finishedMs)},
            {"waiting_ms", static_cast<double>(job.accumulatedWaitMs)},
            {"turnaround_ms", static_cast<double>(*job.finishedMs - job.submittedMs)},
            {"priority", job.priority}, {"sheets", job.sheets()}, {"state", "completed"}});
    }
    for (const auto& d : result.dispatches) {
        dispatches.append(QJsonObject{{"job_id", QString::fromStdString(d.jobId)},
            {"printer", d.printer}, {"started_ms", static_cast<double>(d.startedMs)},
            {"finished_ms", static_cast<double>(d.finishedMs)}});
    }
    return {{"policy", policy}, {"started_ms", static_cast<double>(result.startedMs)},
        {"finished_ms", static_cast<double>(result.finishedMs)}, {"metrics", metrics(result.metrics)},
        {"jobs", jobs}, {"dispatches", dispatches}};
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("scheduler-replay");
    QCommandLineParser parser;
    parser.setApplicationDescription("Deterministic printing workload replay (no printers or sleeping)");
    parser.addHelpOption();
    parser.addOptions({{{"i", "input"}, "Workload JSON file", "file"},
        {{"o", "output"}, "Atomic JSON report destination", "file"},
        {"policy", "all, fcfs, sjf or priority", "name", "all"},
        {"printers", "Number of identical printers (1..64)", "count", "2"},
        {"page-ms", "Milliseconds per sheet", "ms", "150"},
        {"aging-ms", "Waiting milliseconds per priority point", "ms", "30000"}});
    parser.process(app);
    try {
        if (!parser.positionalArguments().isEmpty() || parser.value("input").isEmpty() || parser.value("output").isEmpty())
            throw std::invalid_argument("--input and --output are required; no positional arguments accepted");
        const auto policy = parser.value("policy");
        if (policy != "all" && policy != "fcfs" && policy != "sjf" && policy != "priority")
            throw std::invalid_argument("policy must be all, fcfs, sjf or priority");
        cps::ReplayOptions options;
        options.printers = static_cast<int>(argument(parser, "printers", 1, 64));
        options.pageMs = argument(parser, "page-ms", 1, cps::MaxTimeMs / 10000);
        options.agingIntervalMs = argument(parser, "aging-ms", 1, cps::MaxTimeMs);
        QFile input(parser.value("input"));
        if (!input.open(QIODevice::ReadOnly) || input.size() > 64 * 1024 * 1024)
            throw std::runtime_error("cannot read workload or workload exceeds 64 MiB");
        if (QFileInfo(input).canonicalFilePath() == QFileInfo(parser.value("output")).canonicalFilePath())
            throw std::invalid_argument("output must not replace input");
        const auto bytes = input.readAll();
        if (input.error() != QFileDevice::NoError) throw std::runtime_error("workload read failed");
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(bytes, &error);
        const auto root = doc.object();
        if (error.error != QJsonParseError::NoError || !doc.isObject() ||
            number(root, "schema_version", 1, 1) != 1 || root.value("kind").toString() != "campus-print-workload" ||
            !root.value("jobs").isArray() || (root.contains("source") && !root.value("source").isObject()))
            throw std::invalid_argument("expected campus-print-workload schema_version 1");
        const auto rows = root.value("jobs").toArray();
        if (rows.size() > 10000) throw std::invalid_argument("maximum workload size is 10000 jobs");
        std::vector<cps::Job> workload;
        const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
        for (const auto& row : rows) {
            if (!row.isObject()) throw std::invalid_argument("each job must be an object");
            const auto object = row.toObject();
            const auto id = object.value("job_id").toString();
            const auto title = object.value("title").toString();
            if (!safeId.match(id).hasMatch() || title.trimmed().isEmpty() || title.size() > 200)
                throw std::invalid_argument("invalid job_id or title");
            cps::Job job;
            job.id = id.toStdString();
            job.title = title.toUtf8().toStdString();
            job.pages = static_cast<int>(number(object, "pages", 1, 1000));
            job.copies = static_cast<int>(number(object, "copies", 1, 100));
            job.submittedMs = number(object, "submitted_ms", 0, cps::MaxTimeMs);
            if (object.contains("priority")) job.priority = static_cast<int>(number(object, "priority", 0, 100));
            workload.push_back(std::move(job));
        }
        QJsonArray results;
        const std::pair<QString, cps::Policy> policies[] = {
            {"fcfs", cps::Policy::Fcfs}, {"sjf", cps::Policy::ShortestJobFirst},
            {"priority", cps::Policy::PriorityAging}};
        for (const auto& entry : policies) {
            if (policy == "all" || policy == entry.first)
                results.append(encode(cps::replay(workload, entry.second, options), entry.first));
        }
        const QJsonObject report{{"schema_version", 1}, {"kind", "campus-print-replay"},
            {"model", "non-preemptive; identical printers; no failures; zero dispatch overhead"},
            {"workload_sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"source", root.value("source").toObject()},
            {"options", QJsonObject{{"printers", options.printers}, {"page_ms", static_cast<double>(options.pageMs)},
                {"aging_ms", static_cast<double>(options.agingIntervalMs)}}}, {"results", results}};
        QSaveFile output(parser.value("output"));
        const auto data = QJsonDocument(report).toJson();
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
            throw std::runtime_error("cannot atomically write report; check destination directory");
        QTextStream(stdout) << "Wrote " << results.size() << " policies, " << workload.size() << " jobs per policy\n";
        return 0;
    } catch (const std::exception& error) {
        QTextStream(stderr) << "scheduler-replay: " << error.what() << '\n';
        return 64;
    }
}
