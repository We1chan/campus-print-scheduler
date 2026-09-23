#include "cps/runtime/state_store.hpp"
#include "cps/runtime/shop_io.hpp"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <cmath>
#include <limits>
#include <set>

namespace cps {
namespace {

bool integerValue(const QJsonValue& value, qint64 min, qint64 max, qint64& result) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(min) || number > static_cast<double>(max)) {
        return false;
    }
    result = static_cast<qint64>(number);
    return true;
}

bool intValue(const QJsonObject& object, const char* key, int min, int max, int& result) {
    qint64 parsed = 0;
    if (!integerValue(object.value(QLatin1String(key)), min, max, parsed)) return false;
    result = static_cast<int>(parsed);
    return true;
}

bool timeValue(const QJsonObject& object, const char* key, TimeMs& result) {
    qint64 parsed = 0;
    if (!integerValue(object.value(QLatin1String(key)), 0, MaxTimeMs, parsed)) return false;
    result = static_cast<TimeMs>(parsed);
    return true;
}

bool optionalTimeValue(const QJsonObject& object, const char* key,
                       std::optional<TimeMs>& result) {
    const auto value = object.value(QLatin1String(key));
    if (value.isNull()) {
        result.reset();
        return true;
    }
    qint64 parsed = 0;
    if (!integerValue(value, 0, MaxTimeMs, parsed)) return false;
    result = static_cast<TimeMs>(parsed);
    return true;
}

QString stateName(JobState state) {
    switch (state) {
    case JobState::Queued: return QStringLiteral("queued");
    case JobState::Running: return QStringLiteral("running");
    case JobState::Completed: return QStringLiteral("completed");
    case JobState::Failed: return QStringLiteral("failed");
    case JobState::Cancelling: return QStringLiteral("cancelling");
    case JobState::Cancelled: return QStringLiteral("cancelled");
    }
    return {};
}

std::optional<JobState> stateFromName(const QString& name) {
    if (name == QLatin1String("queued")) return JobState::Queued;
    if (name == QLatin1String("running")) return JobState::Running;
    if (name == QLatin1String("completed")) return JobState::Completed;
    if (name == QLatin1String("failed")) return JobState::Failed;
    if (name == QLatin1String("cancelling")) return JobState::Cancelling;
    if (name == QLatin1String("cancelled")) return JobState::Cancelled;
    return std::nullopt;
}

QJsonValue optionalTime(const std::optional<TimeMs>& value) {
    return value ? QJsonValue(static_cast<double>(*value)) : QJsonValue(QJsonValue::Null);
}

} // namespace

QString coordinatorSocketName(const QString& stateDirectory) {
    const QFileInfo info(stateDirectory);
    QString path = info.canonicalFilePath();
    if (path.isEmpty()) path = QDir(stateDirectory).absolutePath();
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    const auto digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("campus-print-%1").arg(QString::fromLatin1(digest));
}

QString policyName(Policy policy) {
    switch (policy) {
    case Policy::Fcfs: return QStringLiteral("fcfs");
    case Policy::ShortestJobFirst: return QStringLiteral("sjf");
    case Policy::PriorityAging: return QStringLiteral("priority");
    case Policy::Deadline: return QStringLiteral("deadline");
    case Policy::Revenue: return QStringLiteral("revenue");
    }
    return {};
}

std::optional<Policy> policyFromName(const QString& name) {
    if (name == QLatin1String("fcfs")) return Policy::Fcfs;
    if (name == QLatin1String("sjf")) return Policy::ShortestJobFirst;
    if (name == QLatin1String("priority")) return Policy::PriorityAging;
    if (name == "deadline") return Policy::Deadline;
    if (name == "revenue") return Policy::Revenue;
    return std::nullopt;
}

QJsonObject jobSnapshotToJson(const Job& job) {
    QJsonArray runs;
    for (const auto& run : job.runs) {
        runs.append(QJsonObject{
            {"started_ms", static_cast<double>(run.startedMs)},
            {"stopped_ms", optionalTime(run.stoppedMs)}
        });
    }
    QJsonObject object{
        {"job_id", QString::fromStdString(job.id)},
        {"title", QString::fromUtf8(job.title.data(), static_cast<qsizetype>(job.title.size()))},
        {"pages", job.pages},
        {"copies", job.copies},
        {"submitted_ms", static_cast<double>(job.submittedMs)},
        {"state", stateName(job.state)},
        {"priority", job.priority},
        {"max_attempts", job.maxAttempts},
        {"attempts", job.attempts},
        {"queued_since_ms", static_cast<double>(job.queuedSinceMs)},
        {"accumulated_wait_ms", static_cast<double>(job.accumulatedWaitMs)},
        {"last_changed_ms", static_cast<double>(job.lastChangedMs)},
        {"first_started_ms", optionalTime(job.firstStartedMs)},
        {"finished_ms", optionalTime(job.finishedMs)},
        {"runs", runs}
    };
    const auto terms = orderTermsToJson(job);
    for (auto it = terms.begin(); it != terms.end(); ++it) object.insert(it.key(), it.value());
    return object;
}

bool jobSnapshotFromJson(const QJsonObject& object, Job& job, QString& error) {
    const QString id = object.value("job_id").toString();
    const QString title = object.value("title").toString();
    const auto parsedState = stateFromName(object.value("state").toString());
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
    if (!safeId.match(id).hasMatch() || title.trimmed().isEmpty() || title.size() > 200 || !parsedState) {
        error = QStringLiteral("Invalid job identity or state");
        return false;
    }

    Job parsed;
    parsed.id = id.toStdString();
    parsed.title = title.toUtf8().toStdString();
    parsed.state = *parsedState;
    if (!intValue(object, "pages", 1, 1000, parsed.pages) ||
        !intValue(object, "copies", 1, 100, parsed.copies) ||
        parsed.pages * parsed.copies > 10000 ||
        !intValue(object, "priority", 0, 100, parsed.priority) ||
        !intValue(object, "max_attempts", 1, 100, parsed.maxAttempts) ||
        !intValue(object, "attempts", 0, 100, parsed.attempts) ||
        !timeValue(object, "submitted_ms", parsed.submittedMs) ||
        !timeValue(object, "queued_since_ms", parsed.queuedSinceMs) ||
        !timeValue(object, "accumulated_wait_ms", parsed.accumulatedWaitMs) ||
        !timeValue(object, "last_changed_ms", parsed.lastChangedMs) ||
        !optionalTimeValue(object, "first_started_ms", parsed.firstStartedMs) ||
        !optionalTimeValue(object, "finished_ms", parsed.finishedMs)) {
        error = QStringLiteral("Invalid numeric job snapshot field");
        return false;
    }

    const auto runs = object.value("runs");
    if (!runs.isArray()) {
        error = QStringLiteral("Job runs must be an array");
        return false;
    }
    for (const auto& value : runs.toArray()) {
        if (!value.isObject()) {
            error = QStringLiteral("Invalid run entry");
            return false;
        }
        RunInterval run;
        const auto runObject = value.toObject();
        if (!timeValue(runObject, "started_ms", run.startedMs) ||
            !optionalTimeValue(runObject, "stopped_ms", run.stoppedMs)) {
            error = QStringLiteral("Invalid run time");
            return false;
        }
        parsed.runs.push_back(run);
    }
    if (!orderTermsFromJson(object, parsed, true)) { error = "Invalid commercial order terms"; return false; }
    job = std::move(parsed);
    return true;
}

bool writePersistentState(const QString& path, const PersistentState& state, QString& error) {
    QJsonArray jobs;
    for (const auto& job : state.jobs) jobs.append(jobSnapshotToJson(job));
    QJsonObject object{
        {"schema_version", state.fleet.empty() ? 1 : 2},
        {"kind", QStringLiteral("campus-print-state")},
        {"saved_at_ms", static_cast<double>(state.savedAtMs)},
        {"policy", policyName(state.policy)},
        {"printers", state.printers},
        {"paused", state.paused},
        {"aging_interval_ms", static_cast<double>(state.schedulerOptions.agingIntervalMs)},
        {"jobs", jobs}
    };
    if (!state.fleet.empty()) {
        QJsonArray fleet;
        for (const auto& p : state.fleet) fleet.append(printerToJson(p));
        object.insert("shop", QJsonObject{{"version", 1}, {"fleet", fleet}, {"supplied_a4", state.supplied.a4}, {"supplied_a3", state.supplied.a3}});
    }
    QSaveFile file(path);
    const auto data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

bool readPersistentState(const QString& path, PersistentState& state, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
        error = QStringLiteral("Cannot open persistent state or file exceeds 64 MiB");
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid persistent state JSON");
        return false;
    }
    const auto object = document.object();
    qint64 schema = 0;
    qint64 saved = 0;
    qint64 aging = 0;
    int printers = 0;
    const auto policy = policyFromName(object.value("policy").toString());
    if (!integerValue(object.value("schema_version"), 1, 2, schema) ||
        object.value("kind").toString() != QLatin1String("campus-print-state") ||
        !integerValue(object.value("saved_at_ms"), 0, MaxTimeMs, saved) ||
        !integerValue(object.value("aging_interval_ms"), 1, MaxTimeMs, aging) ||
        !intValue(object, "printers", 1, 64, printers) || !policy ||
        !object.value("jobs").isArray() ||
        (object.contains("paused") && !object.value("paused").isBool())) {
        error = QStringLiteral("Invalid persistent state header");
        return false;
    }

    PersistentState parsed;
    parsed.savedAtMs = static_cast<TimeMs>(saved);
    parsed.schedulerOptions.agingIntervalMs = static_cast<TimeMs>(aging);
    parsed.policy = *policy;
    parsed.printers = printers;
    parsed.paused = object.value("paused").toBool(false);
    for (const auto& value : object.value("jobs").toArray()) {
        if (!value.isObject()) {
            error = QStringLiteral("Persistent jobs must be objects");
            return false;
        }
        Job job;
        if (!jobSnapshotFromJson(value.toObject(), job, error)) return false;
        parsed.jobs.push_back(std::move(job));
    }

    if ((schema == 2) != object.contains("shop")) { error = "Shop state version mismatch"; return false; }
    if (object.contains("shop")) {
        const auto shop = object.value("shop").toObject();
        if (shop.value("version").toDouble() != 1 || !shop.value("fleet").isArray() ||
            shop.value("fleet").toArray().size() != printers ||
            !intValue(shop, "supplied_a4", 0, 1000000000, parsed.supplied.a4) ||
            !intValue(shop, "supplied_a3", 0, 1000000000, parsed.supplied.a3)) {
            error = "Invalid shop state"; return false;
        }
        for (const auto& value : shop.value("fleet").toArray()) {
            Printer p;
            if (!value.isObject() || !printerFromJson(value.toObject(), p)) { error = "Invalid printer"; return false; }
            parsed.fleet.push_back(p);
        }
        const auto available = availablePaper(parsed.jobs, parsed.supplied);
        if (available.a4 < 0 || available.a3 < 0) { error = "Paper over-reserved"; return false; }
        std::set<int> occupied;
        for (const auto& j : parsed.jobs) {
            if (j.assignedPrinter >= printers || (j.attempts > 0 && j.assignedPrinter < 0)) { error = "Invalid assigned printer"; return false; }
            if ((j.state == JobState::Running || j.state == JobState::Cancelling) &&
                !occupied.insert(j.assignedPrinter).second) { error = "Printer assigned twice"; return false; }
        }
    }
    Scheduler validator(parsed.schedulerOptions, [now = parsed.savedAtMs] { return now; });
    if (!validator.restore(parsed.jobs, parsed.savedAtMs)) {
        error = QStringLiteral("Persistent scheduler snapshot is inconsistent");
        return false;
    }
    state = std::move(parsed);
    return true;
}

} // namespace cps
