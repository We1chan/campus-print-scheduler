#include "cps/core/scheduler.hpp"
#include "cps/metrics/statistics.hpp"
#include "cps/platform/daemon.hpp"
#include "cps/runtime/job_io.hpp"
#include "cps/runtime/state_store.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

cps::TimeMs wallNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool strictInt(const QJsonValue& value, int min, int max, int& result) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < min || number > max || std::floor(number) != number) return false;
    result = static_cast<int>(number);
    return true;
}

bool strictTime(const QJsonValue& value, cps::TimeMs max, cps::TimeMs& result) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number > static_cast<double>(max)) return false;
    result = static_cast<cps::TimeMs>(number);
    return true;
}

QJsonObject statisticsToJson(const cps::Statistics& stats) {
    return {
        {"submitted", static_cast<double>(stats.submitted)},
        {"first_started", static_cast<double>(stats.firstStarted)},
        {"completed", static_cast<double>(stats.completed)},
        {"failed", static_cast<double>(stats.failed)},
        {"cancelled", static_cast<double>(stats.cancelled)},
        {"queued", static_cast<double>(stats.queued)},
        {"running", static_cast<double>(stats.running)},
        {"cancelling", static_cast<double>(stats.cancelling)},
        {"average_first_wait_ms", stats.averageFirstWaitMs},
        {"max_first_wait_ms", static_cast<double>(stats.maxFirstWaitMs)},
        {"average_total_queue_wait_ms", stats.averageTotalQueueWaitMs},
        {"average_turnaround_ms", stats.averageTurnaroundMs},
        {"max_current_queue_wait_ms", static_cast<double>(stats.maxCurrentQueueWaitMs)},
        {"busy_printer_ms", stats.busyPrinterMs},
        {"throughput_per_second", stats.throughputPerSecond},
        {"utilization", stats.utilization}
    };
}

// Windows named pipes allow several servers on one name, so QLocalServer::listen
// cannot detect a live coordinator using a different state directory. A lock file
// keyed by the socket name in the system temp dir works identically on all platforms.
QString socketLockPath(const QString& socketName) {
    const auto digest = QCryptographicHash::hash(
        socketName.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
    return QDir::tempPath() + QStringLiteral("/campus-print-coordinator-%1.lock")
        .arg(QString::fromLatin1(digest));
}

class Coordinator : public QObject {
public:
    Coordinator(QString stateDirectory, QString socketName, cps::PersistentState initial,
                int pageMs, int maxRestarts, int timeoutMs, bool demoMode, QObject* parent = nullptr)
        : QObject(parent),
          stateDirectory_(QDir(stateDirectory).absolutePath()),
          jobsDirectory_(QDir(stateDirectory_).filePath("jobs")),
          outputDirectory_(QDir(stateDirectory_).filePath("output")),
          controlDirectory_(QDir(stateDirectory_).filePath("control")),
          statePath_(QDir(stateDirectory_).filePath("state.json")),
          socketName_(std::move(socketName)),
          state_(std::move(initial)),
          pageMs_(pageMs),
          maxRestarts_(maxRestarts),
          timeoutMs_(timeoutMs),
          demoMode_(demoMode),
          instanceLock_(QDir(stateDirectory_).filePath(".coordinator.lock")),
          socketLock_(socketLockPath(socketName_)),
          server_(this),
          recoveryTimer_(this) {}

    int start(QString& error) {
        if (!QDir().mkpath(stateDirectory_) || !QDir().mkpath(jobsDirectory_) ||
            !QDir().mkpath(outputDirectory_) || !QDir().mkpath(controlDirectory_)) {
            error = QStringLiteral("Cannot create coordinator state directories");
            return 73;
        }

        instanceLock_.setStaleLockTime(0);
        if (!instanceLock_.tryLock(0)) {
            error = QStringLiteral("Another coordinator already owns this state directory");
            return 75;
        }

        socketLock_.setStaleLockTime(0);
        if (!socketLock_.tryLock(0)) {
            error = QStringLiteral("Coordinator socket name is already in use");
            return 75;
        }

        const bool hasState = QFileInfo::exists(statePath_);
        if (hasState && !cps::readPersistentState(statePath_, state_, error)) return 64;

        const cps::TimeMs epoch = std::max(wallNowMs(), hasState ? state_.savedAtMs : cps::TimeMs{0});
        const auto anchor = std::chrono::steady_clock::now();
        scheduler_ = std::make_unique<cps::Scheduler>(
            state_.schedulerOptions,
            [epoch, anchor] {
                return epoch + std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - anchor).count();
            });

        if (hasState && !scheduler_->restore(state_.jobs, scheduler_->nowMs())) {
            error = QStringLiteral("Persistent state cannot be restored by Scheduler");
            return 64;
        }
        if (!hasState && !persist(error)) return 73;

        for (const auto& job : scheduler_->snapshot()) {
            if (job.state == cps::JobState::Running || job.state == cps::JobState::Cancelling) {
                recovering_.insert(QString::fromStdString(job.id));
                recoveryMisses_.insert(QString::fromStdString(job.id), 0);
                slots_.insert(QString::fromStdString(job.id), static_cast<int>(slots_.size()));
            }
        }
        garbageCollectLeftovers();

        if (!server_.listen(socketName_)) {
            QLocalSocket endpointProbe;
            endpointProbe.connectToServer(socketName_);
            if (endpointProbe.waitForConnected(150)) {
                endpointProbe.disconnectFromServer();
                error = QStringLiteral("Coordinator socket name is already in use");
                return 75;
            }
            if (!QLocalServer::removeServer(socketName_) || !server_.listen(socketName_)) {
                error = server_.errorString();
                return 73;
            }
        }
        connect(&server_, &QLocalServer::newConnection, this, [this] { acceptClients(); });
        connect(&recoveryTimer_, &QTimer::timeout, this, [this] { reconcileRecovery(); });
        recoveryTimer_.start(200);
        QTimer::singleShot(0, this, [this] { dispatch(); });
        return 0;
    }

private:
    struct ActiveJob {
        QProcess* process = nullptr;
        int attempt = 0;
        QByteArray stdoutBuffer;
        QString cancelFile;
        int printed = 0;
    };

    bool persist(QString& error) {
        state_.savedAtMs = scheduler_->nowMs();
        state_.jobs = scheduler_->snapshot();
        return cps::writePersistentState(statePath_, state_, error);
    }

    bool persistOrStop() {
        QString error;
        if (persist(error)) return true;
        broadcast({{"schema_version", 1}, {"type", "fatal"}, {"reason", error}});
        std::fprintf(stderr, "Persistent state write failed: %s\n", qPrintable(error));
        QCoreApplication::exit(73);
        return false;
    }

    std::optional<cps::Job> findJob(const QString& id) const {
        for (const auto& job : scheduler_->snapshot()) {
            if (QString::fromStdString(job.id) == id) return job;
        }
        return std::nullopt;
    }

    QString outputPath(const QString& id) const {
        return QDir(outputDirectory_).filePath(id + ".txt");
    }

    QString supervisorLockPath(const QString& id) const {
        return QDir(outputDirectory_).filePath("." + id + ".supervisor.lock");
    }

    QString outputLockPath(const QString& id) const {
        return QDir(outputDirectory_).filePath("." + id + ".output.lock");
    }

    bool lockIsFree(const QString& path) const {
        QLockFile probe(path);
        probe.setStaleLockTime(0);
        if (!probe.tryLock(0)) return false;
        probe.unlock();
        return true;
    }

    QString cancelPath(const QString& id, int attempt) const {
        return QDir(controlDirectory_).filePath(
            QStringLiteral("%1.%2.cancel").arg(id).arg(attempt));
    }

    QString jobFilePath(const QString& id) const {
        return QDir(jobsDirectory_).filePath(id + QStringLiteral(".json"));
    }

    static bool attemptIsLive(cps::JobState state) {
        return state == cps::JobState::Running || state == cps::JobState::Cancelling;
    }

    // Crash leftovers must not accumulate: drop job descriptors that no live
    // attempt reads and cancel markers that match no current Running/Cancelling
    // attempt. Dispatch rewrites the descriptor before every supervisor start,
    // so anything outside a live attempt is garbage.
    void garbageCollectLeftovers() {
        QSet<QString> liveMarkers;
        QHash<QString, cps::JobState> states;
        for (const auto& job : scheduler_->snapshot()) {
            const QString id = QString::fromStdString(job.id);
            states.insert(id, job.state);
            if (attemptIsLive(job.state)) liveMarkers.insert(cancelPath(id, job.attempts));
        }
        for (const auto& entry : QDir(controlDirectory_).entryList(QDir::Files)) {
            const QString path = QDir(controlDirectory_).filePath(entry);
            if (!liveMarkers.contains(path)) QFile::remove(path);
        }
        for (const auto& entry : QDir(jobsDirectory_).entryList(QStringList() << QStringLiteral("*.json"), QDir::Files)) {
            const QString id = entry.left(entry.size() - 5);
            if (!attemptIsLive(states.value(id, cps::JobState::Cancelled))) {
                QFile::remove(jobFilePath(id));
            }
        }
    }

    bool createCancelMarker(const QString& id, int attempt) {
        QSaveFile marker(cancelPath(id, attempt));
        return marker.open(QIODevice::WriteOnly) && marker.commit();
    }

    void acceptClients() {
        while (server_.hasPendingConnections()) {
            auto* socket = server_.nextPendingConnection();
            clientBuffers_.insert(socket, {});
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                auto& buffer = clientBuffers_[socket];
                buffer += socket->readAll();
                if (buffer.size() > 8 * 1024 * 1024) {
                    socket->disconnectFromServer();
                    return;
                }
                while (true) {
                    const qsizetype newline = buffer.indexOf('\n');
                    if (newline < 0) break;
                    const QByteArray line = buffer.left(newline);
                    buffer.remove(0, newline + 1);
                    if (!line.trimmed().isEmpty()) handleCommand(socket, line);
                }
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                clientBuffers_.remove(socket);
                socket->deleteLater();
            });
        }
    }

    void send(QLocalSocket* socket, QJsonObject object) {
        if (!socket) return;
        object.insert("schema_version", 1);
        if (socket->bytesToWrite() > 32 * 1024 * 1024) { socket->abort(); return; }
        socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
    }

    void reply(QLocalSocket* socket, const QString& requestId, QJsonObject object) {
        object.insert("request_id", requestId);
        send(socket, std::move(object));
    }

    void broadcast(QJsonObject object) {
        const auto sockets = clientBuffers_.keys();
        for (auto* socket : sockets) {
            if (socket->state() == QLocalSocket::ConnectedState) send(socket, object);
        }
    }

    void broadcastState(const QString& id) {
        const auto job = findJob(id);
        if (!job) return;
        broadcast({
            {"type", "job_state"},
            {"job_id", id},
            {"job", cps::jobSnapshotToJson(*job)}
        });
    }

    void commandError(QLocalSocket* socket, const QString& requestId, const QString& reason) {
        reply(socket, requestId, {{"type", "error"}, {"reason", reason}});
    }

    bool parseSubmission(const QJsonObject& object, cps::TimeMs submitted, cps::Job& job) {
        static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
        const auto id = object.value("job_id").toString();
        const auto title = object.value("title").toString().trimmed();
        if (!safeId.match(id).hasMatch() || title.isEmpty() || title.size() > 200 ||
            !strictInt(object.value("pages"), 1, 1000, job.pages) ||
            !strictInt(object.value("copies"), 1, 100, job.copies) || job.sheets() > 10000 ||
            (object.contains("priority") && !strictInt(object.value("priority"), 0, 100, job.priority)) ||
            (object.contains("max_attempts") && !strictInt(object.value("max_attempts"), 1, 100, job.maxAttempts)) ||
            QFileInfo(outputPath(id)).isFile()) return false;
        job.id = id.toStdString(); job.title = title.toUtf8().toStdString(); job.submittedMs = submitted;
        return true;
    }

    QJsonObject snapshot() const {
        const auto jobs = scheduler_->snapshot();
        const auto now = scheduler_->nowMs();
        auto start = now;
        QJsonArray rows, order;
        for (const auto& job : jobs) {
            start = std::min(start, job.submittedMs);
            const auto id = QString::fromStdString(job.id);
            auto row = cps::jobSnapshotToJson(job);
            row.insert("slot", slots_.value(id, -1));
            row.insert("recovering", recovering_.contains(id));
            row.insert("printed_sheets", job.state == cps::JobState::Completed ? job.sheets() :
                       active_.value(id).printed);
            rows.append(row);
        }
        for (const auto& id : scheduler_->waitingOrder(state_.policy, now)) order.append(QString::fromStdString(id));
        const auto metrics = statisticsToJson(cps::statistics(jobs, start, now, state_.printers));
        return {{"type", "snapshot"}, {"policy", cps::policyName(state_.policy)}, {"paused", state_.paused},
            {"printers", state_.printers}, {"page_ms", pageMs_}, {"demo_mode", demoMode_},
            {"coordinator_pid", static_cast<double>(QCoreApplication::applicationPid())},
            {"aging_interval_ms", static_cast<double>(state_.schedulerOptions.agingIntervalMs)},
            {"window_start_ms", static_cast<double>(start)}, {"now_ms", static_cast<double>(now)},
            {"jobs", rows}, {"waiting_order", order}, {"metrics", metrics}};
    }

    void handleCommand(QLocalSocket* socket, const QByteArray& line) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            commandError(socket, {}, QStringLiteral("invalid JSON command"));
            return;
        }
        const auto command = document.object();
        const QString requestId = command.value("request_id").toString();
        const QString type = command.value("type").toString();
        int schemaVersion = 0;
        if (!strictInt(command.value("schema_version"), 1, 1, schemaVersion) ||
            requestId.isEmpty() || type.isEmpty()) {
            commandError(socket, requestId, QStringLiteral("schema_version, request_id and type are required"));
            return;
        }

        if (type == QLatin1String("snapshot")) {
            reply(socket, requestId, snapshot());
            return;
        }

        if (type == QLatin1String("submit") || type == QLatin1String("submit_batch")) {
            const QJsonArray input = type == QLatin1String("submit")
                ? QJsonArray{command.value("job")} : command.value("jobs").toArray();
            if (stopping_ || input.isEmpty() || scheduler_->snapshot().size() +
                    static_cast<std::size_t>(input.size()) > 10000) {
                commandError(socket, requestId, QStringLiteral("empty batch, queue full or shutting down"));
                return;
            }
            std::vector<cps::Job> jobs;
            const auto submitted = scheduler_->nowMs();
            for (const auto& value : input) {
                cps::Job job;
                if (!value.isObject() || !parseSubmission(value.toObject(), submitted, job)) {
                    commandError(socket, requestId, QStringLiteral("invalid job fields or output already exists"));
                    return;
                }
                jobs.push_back(std::move(job));
            }
            if (!scheduler_->submitBatch(jobs)) {
                commandError(socket, requestId, QStringLiteral("job rejected or duplicate job_id"));
                return;
            }
            if (!persistOrStop()) return;
            reply(socket, requestId, {{"type", "accepted"}, {"count", input.size()},
                {"job_id", QString::fromStdString(jobs.front().id)}});
            // One notification per batch avoids flooding clients with thousands of events.
            broadcast({{"type", "queue_changed"}});
            dispatch();
            return;
        }

        if (type == QLatin1String("set_paused")) {
            if (!command.value("paused").isBool()) {
                commandError(socket, requestId, QStringLiteral("paused must be boolean"));
                return;
            }
            state_.paused = command.value("paused").toBool();
            if (!persistOrStop()) return;
            reply(socket, requestId, {{"type", "accepted"}});
            dispatch();
            return;
        }
        if (type == QLatin1String("crash_worker")) {
            const auto id = command.value("job_id").toString();
            const auto job = findJob(id);
            if (!demoMode_ || !job || job->state != cps::JobState::Running || !active_.contains(id)) {
                commandError(socket, requestId, QStringLiteral("demo disabled or job not running"));
                return;
            }
            QSaveFile fault(cancelPath(id, job->attempts) + ".fault");
            if (!fault.open(QIODevice::WriteOnly) || !fault.commit()) {
                commandError(socket, requestId, QStringLiteral("cannot write fault marker"));
                return;
            }
            reply(socket, requestId, {{"type", "accepted"}});
            return;
        }

        if (type == QLatin1String("cancel")) {
            const QString id = command.value("job_id").toString();
            const auto before = findJob(id);
            if (!before) {
                commandError(socket, requestId, QStringLiteral("job cannot be cancelled"));
                return;
            }
            const bool running = before->state == cps::JobState::Running;
            if (running && !createCancelMarker(id, before->attempts)) {
                commandError(socket, requestId, QStringLiteral("cannot persist cancellation intent"));
                return;
            }
            if (!scheduler_->cancel(id.toStdString(), scheduler_->nowMs())) {
                if (running) QFile::remove(cancelPath(id, before->attempts));
                commandError(socket, requestId, QStringLiteral("job cannot be cancelled"));
                return;
            }
            const auto after = findJob(id);
            if (!persistOrStop()) return;
            reply(socket, requestId, {{"type", "accepted"}, {"job_id", id}});
            broadcastState(id);
            return;
        }

        if (type == QLatin1String("set_policy")) {
            const auto policy = cps::policyFromName(command.value("policy").toString());
            if (!policy) {
                commandError(socket, requestId, QStringLiteral("unknown policy"));
                return;
            }
            state_.policy = *policy;
            if (!persistOrStop()) return;
            reply(socket, requestId, {{"type", "accepted"}, {"policy", cps::policyName(*policy)}});
            dispatch();
            return;
        }

        if (type == QLatin1String("stats")) {
            const auto now = scheduler_->nowMs();
            const auto startValue = command.value("window_start_ms");
            const auto endValue = command.value("window_end_ms");
            cps::TimeMs start = 0;
            cps::TimeMs end = now;
            if ((!startValue.isUndefined() && !strictTime(startValue, now, start)) ||
                (!endValue.isUndefined() && !strictTime(endValue, now, end)) || start > end) {
                commandError(socket, requestId,
                             QStringLiteral("window_start_ms/window_end_ms must be integers in [0, now]"));
                return;
            }
            try {
                const auto stats = cps::statistics(scheduler_->snapshot(), start, end, state_.printers);
                reply(socket, requestId, {
                    {"type", "stats"},
                    {"window_start_ms", static_cast<double>(start)},
                    {"window_end_ms", static_cast<double>(end)},
                    {"statistics", statisticsToJson(stats)}
                });
            } catch (const std::invalid_argument&) {
                commandError(socket, requestId, QStringLiteral("invalid statistics window"));
            }
            return;
        }

        if (type == QLatin1String("shutdown")) {
            stopping_ = true;
            server_.close();
            reply(socket, requestId, {{"type", "accepted"}});
            maybeFinishShutdown();
            return;
        }

        commandError(socket, requestId, QStringLiteral("unknown command"));
    }

    void dispatch() {
        if (stopping_ || state_.paused) return;
        while (active_.size() + recovering_.size() < state_.printers) {
            const auto job = scheduler_->claim(state_.policy);
            if (!job) break;
            const QString id = QString::fromStdString(job->id);
            if (!persistOrStop()) return;

            const QString jobPath = QDir(jobsDirectory_).filePath(id + ".json");
            QString error;
            if (!cps::writeJob(jobPath, *job, error)) {
                scheduler_->retry(job->id, job->attempts, scheduler_->nowMs());
                if (!persistOrStop()) return;
                broadcastState(id);
                // Disk failures can persist; back off across dispatch passes
                // instead of spinning the claim/write loop in one call.
                dispatchBackoffMs_ = dispatchBackoffMs_ == 0
                    ? 200 : std::min(dispatchBackoffMs_ * 2, 5000);
                QTimer::singleShot(dispatchBackoffMs_, this, [this] { dispatch(); });
                return;
            }
            dispatchBackoffMs_ = 0;

            const QString cancellation = cancelPath(id, job->attempts);
            QFile::remove(cancellation);
            auto* process = new QProcess(this);
            process->setProgram(cps::siblingExecutable("print-supervisor"));
            QStringList arguments{
                "--job", jobPath,
                "--output-dir", outputDirectory_,
                "--page-ms", QString::number(pageMs_),
                "--max-restarts", QString::number(maxRestarts_),
                "--timeout-ms", QString::number(timeoutMs_),
                "--cancel-file", cancellation,
                "--claim-attempt", QString::number(job->attempts)
            };
            if (demoMode_) arguments << "--demo-mode" << "--fault-file" << cancellation + ".fault";
            process->setArguments(arguments);
            int slot = 0;
            while (slots_.values().contains(slot)) ++slot;
            slots_.insert(id, slot);
            active_.insert(id, ActiveJob{process, job->attempts, {}, cancellation});

            connect(process, &QProcess::readyReadStandardOutput, this, [this, id] {
                auto it = active_.find(id);
                if (it == active_.end()) return;
                it->stdoutBuffer += it->process->readAllStandardOutput();
                while (true) {
                    const qsizetype newline = it->stdoutBuffer.indexOf('\n');
                    if (newline < 0) break;
                    const QByteArray line = it->stdoutBuffer.left(newline);
                    it->stdoutBuffer.remove(0, newline + 1);
                    QJsonParseError error;
                    const auto document = QJsonDocument::fromJson(line, &error);
                    if (error.error == QJsonParseError::NoError && document.isObject()) {
                        auto event = document.object();
                        if (event.value("job_id").toString() != id) continue;
                        event.insert("claim_attempt", it->attempt);
                        const auto kind = event.value("type").toString();
                        if (kind == "progress") it->printed = event.value("printed_sheets").toInt();
                        if (kind == "worker_restarting") it->printed = 0;
                        broadcast(event);
                    }
                }
            });
            connect(process, &QProcess::readyReadStandardError, this, [process] {
                const auto bytes = process->readAllStandardError();
                std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stderr);
            });
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                    [this, id](int code, QProcess::ExitStatus status) {
                        finalizeExecution(id, code, status);
                    });
            connect(process, &QProcess::errorOccurred, this,
                    [this, id](QProcess::ProcessError error) {
                        if (error == QProcess::FailedToStart) {
                            QTimer::singleShot(0, this, [this, id] {
                                finalizeExecution(id, 78, QProcess::CrashExit);
                            });
                        }
                    });
            process->start();
            broadcastState(id);
        }
    }

    void finalizeExecution(const QString& id, int code, QProcess::ExitStatus status) {
        auto it = active_.find(id);
        if (it == active_.end()) return;
        const ActiveJob active = it.value();
        active_.erase(it);
        slots_.remove(id);

        const auto job = findJob(id);
        const bool outputCommitted = QFileInfo(outputPath(id)).isFile();
        const bool success = status == QProcess::NormalExit && code == 0 && outputCommitted;
        const auto now = scheduler_->nowMs();
        if (job) {
            if (success || outputCommitted) {
                scheduler_->finish(id.toStdString(), active.attempt, true, now);
            } else if (job->state == cps::JobState::Cancelling) {
                scheduler_->acknowledgeCancel(id.toStdString(), active.attempt, now);
            } else {
                scheduler_->retry(id.toStdString(), active.attempt, now);
            }
        }
        QFile::remove(active.cancelFile);
        QFile::remove(active.cancelFile + ".fault");
        QFile::remove(jobFilePath(id));
        active.process->deleteLater();
        if (!persistOrStop()) return;
        broadcastState(id);
        if (stopping_) maybeFinishShutdown();
        else dispatch();
    }

    void reconcileRecovery() {
        if (recovering_.isEmpty()) {
            if (stopping_) maybeFinishShutdown();
            return;
        }
        const auto ids = recovering_.values();
        for (const auto& id : ids) {
            auto job = findJob(id);
            if (!job) {
                recovering_.remove(id);
                slots_.remove(id);
                recoveryMisses_.remove(id);
                continue;
            }
            const QString cancellation = cancelPath(id, job->attempts);
            if (job->state == cps::JobState::Running && QFileInfo::exists(cancellation)) {
                if (scheduler_->cancel(job->id, scheduler_->nowMs())) {
                    if (!persistOrStop()) return;
                    job = findJob(id);
                    if (!job) continue;
                    broadcastState(id);
                }
            }
            if (QFileInfo(outputPath(id)).isFile()) {
                scheduler_->finish(job->id, job->attempts, true, scheduler_->nowMs());
                recovering_.remove(id);
                slots_.remove(id);
                recoveryMisses_.remove(id);
                QFile::remove(cancellation);
                QFile::remove(jobFilePath(id));
                if (!persistOrStop()) return;
                broadcastState(id);
                continue;
            }
            if (job->state == cps::JobState::Cancelling && !QFileInfo::exists(cancellation)) {
                createCancelMarker(id, job->attempts);
            }

            if (!lockIsFree(supervisorLockPath(id)) || !lockIsFree(outputLockPath(id))) {
                recoveryMisses_[id] = 0;
                continue;
            }
            const int misses = recoveryMisses_.value(id) + 1;
            recoveryMisses_[id] = misses;
            if (misses < 3) continue;

            if (job->state == cps::JobState::Cancelling) {
                scheduler_->acknowledgeCancel(job->id, job->attempts, scheduler_->nowMs());
            } else {
                scheduler_->retry(job->id, job->attempts, scheduler_->nowMs());
            }
            recovering_.remove(id);
            slots_.remove(id);
            recoveryMisses_.remove(id);
            QFile::remove(cancellation);
            QFile::remove(jobFilePath(id));
            if (!persistOrStop()) return;
            broadcastState(id);
        }
        if (stopping_) maybeFinishShutdown();
        else dispatch();
    }

    void maybeFinishShutdown() {
        if (stopping_ && active_.isEmpty() && recovering_.isEmpty()) {
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        }
    }

    QString stateDirectory_;
    QString jobsDirectory_;
    QString outputDirectory_;
    QString controlDirectory_;
    QString statePath_;
    QString socketName_;
    cps::PersistentState state_;
    int pageMs_ = 100;
    int maxRestarts_ = 3;
    int timeoutMs_ = 30000;
    bool demoMode_ = false;
    std::unique_ptr<cps::Scheduler> scheduler_;
    QLockFile instanceLock_;
    QLockFile socketLock_;
    // Destruction runs in reverse declaration order: clientBuffers_ must outlive
    // server_, whose destructor deletes connected sockets and fires the
    // disconnected lambda that touches clientBuffers_.
    QHash<QLocalSocket*, QByteArray> clientBuffers_;
    QLocalServer server_;
    QHash<QString, ActiveJob> active_;
    int dispatchBackoffMs_ = 0;
    QSet<QString> recovering_;
    QHash<QString, int> recoveryMisses_;
    QTimer recoveryTimer_;
    bool stopping_ = false;
    QHash<QString, int> slots_;
};

} // namespace

int main(int argc, char* argv[]) {
    bool detached = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--daemon")) detached = true;
    }
    if (detached) {
#ifdef Q_OS_WIN
        std::fprintf(stderr, "--daemon is POSIX-only; use foreground coordination on Windows.\n");
        return 69;
#else
        bool absoluteStateDirectory = false;
        for (int i = 1; i + 1 < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--state-dir")) {
                absoluteStateDirectory = QFileInfo(QString::fromLocal8Bit(argv[i + 1])).isAbsolute();
            }
        }
        if (!absoluteStateDirectory) {
            std::fprintf(stderr, "--daemon requires an absolute --state-dir path\n");
            return 64;
        }
        const auto executable = QStandardPaths::findExecutable(QString::fromLocal8Bit(argv[0])).toLocal8Bit();
        if (executable.isEmpty()) return 69;
        std::vector<char*> arguments;
        for (int i = 0; i < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) != QLatin1String("--daemon")) arguments.push_back(argv[i]);
        }
        arguments.push_back(nullptr);
        QString error;
        if (!cps::detachDaemon(executable.constData(), arguments.data(), error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 69;
        }
#endif
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CampusPrint");
    QCoreApplication::setApplicationName("print-coordinator");

    QCommandLineParser parser;
    parser.setApplicationDescription("Persistent local coordinator for simulated campus printing.");
    parser.addHelpOption();
    parser.addOptions({
        {"request", "Send one JSON command to an existing coordinator and print its reply", "json"},
        {"state-dir", "Persistent state directory", "directory", cps::defaultStateDirectory()},
        {"socket-name", "QLocalServer name (default derives from state directory)", "name"},
        {"policy", "Initial policy: fcfs|sjf|priority", "policy", "fcfs"},
        {"printers", "Initial printer slots (1..64)", "count", "2"},
        {"aging-ms", "Initial priority aging interval", "ms", "30000"},
        {"page-ms", "Milliseconds per simulated sheet", "ms", "100"},
        {"max-restarts", "Worker restart limit per supervisor", "count", "3"},
        {"timeout-ms", "Worker silence timeout", "ms", "30000"},
        {"demo-mode", "Allow explicit worker fault injection"},
        {"daemon", "Detach coordinator from the terminal on POSIX"}
    });
    parser.process(app);

    bool okPrinters = false, okAging = false, okPage = false, okRestarts = false, okTimeout = false;
    const int printers = parser.value("printers").toInt(&okPrinters);
    const qint64 aging = parser.value("aging-ms").toLongLong(&okAging);
    const int pageMs = parser.value("page-ms").toInt(&okPage);
    const int maxRestarts = parser.value("max-restarts").toInt(&okRestarts);
    const int timeoutMs = parser.value("timeout-ms").toInt(&okTimeout);
    const auto policy = cps::policyFromName(parser.value("policy"));
    if (!okPrinters || printers < 1 || printers > 64 || !okAging || aging < 1 ||
        aging > cps::MaxTimeMs || !okPage || pageMs < 10 || pageMs > 10000 ||
        !okRestarts || maxRestarts < 0 || maxRestarts > 10 ||
        !okTimeout || timeoutMs < 20000 || !policy) {
        std::fprintf(stderr, "Invalid coordinator options\n");
        return 64;
    }

    const QString stateDirectory = QDir(parser.value("state-dir")).absolutePath();
    QDir().mkpath(stateDirectory);
    const QString socketName = parser.value("socket-name").isEmpty()
        ? cps::coordinatorSocketName(stateDirectory) : parser.value("socket-name");
    if (parser.isSet("request")) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(parser.value("request").toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return 64;
        auto command = document.object();
        command.insert("schema_version", 1);
        command.insert("request_id", "cli-request");
        QLocalSocket socket;
        socket.connectToServer(socketName);
        if (!socket.waitForConnected(3000)) return 69;
        const auto data = QJsonDocument(command).toJson(QJsonDocument::Compact) + '\n';
        if (data.size() > 8 * 1024 * 1024) return 64;
        socket.write(data);
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 5000) {
            while (socket.canReadLine()) {
                const auto line = socket.readLine();
                const auto response = QJsonDocument::fromJson(line).object();
                if (response.value("request_id").toString() != "cli-request") continue;
                std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
                return response.value("type").toString() == "error" ? 64 : 0;
            }
            socket.waitForReadyRead(100);
        }
        return 74;
    }

    cps::PersistentState initial;
    initial.schedulerOptions.agingIntervalMs = aging;
    initial.policy = *policy;
    initial.printers = printers;

    Coordinator coordinator(stateDirectory, socketName, initial, pageMs, maxRestarts, timeoutMs, parser.isSet("demo-mode"));
    QString error;
    const int startCode = coordinator.start(error);
    if (startCode != 0) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return startCode;
    }
    return app.exec();
}
