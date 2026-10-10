#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#ifdef Q_OS_WIN
#include <QDateTime>
#include <QLockFile>
#include <qt_windows.h>
#endif

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class Client {
public:
    void connectTo(const QString& name, int timeoutMs = 5000) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            socket_.abort();
            socket_.connectToServer(name);
            if (socket_.waitForConnected(200)) return;
            QThread::msleep(50);
        }
        throw std::runtime_error("cannot connect to coordinator socket");
    }

    QJsonObject request(const QString& type, QJsonObject object = {}, int timeoutMs = 5000) {
        const QString requestId = QStringLiteral("req-%1").arg(++sequence_);
        object.insert("schema_version", 1);
        object.insert("request_id", requestId);
        object.insert("type", type);
        socket_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
        require(socket_.waitForBytesWritten(1000), "cannot write IPC request");

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            while (socket_.canReadLine()) {
                const auto line = socket_.readLine();
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(line, &error);
                if (error.error != QJsonParseError::NoError || !document.isObject()) continue;
                const auto response = document.object();
                if (response.value("request_id").toString() == requestId) return response;
            }
            socket_.waitForReadyRead(100);
        }
        throw std::runtime_error("timed out waiting for IPC response");
    }

    bool waitForDisconnected(int timeoutMs = 5000) {
        return socket_.state() == QLocalSocket::UnconnectedState || socket_.waitForDisconnected(timeoutMs);
    }

private:
    QLocalSocket socket_;
    int sequence_ = 0;
};

QJsonObject jobFromSnapshot(const QJsonObject& snapshot, const QString& id) {
    for (const auto& value : snapshot.value("jobs").toArray()) {
        const auto job = value.toObject();
        if (job.value("job_id").toString() == id) return job;
    }
    return {};
}

QJsonObject waitForState(Client& client, const QString& id, const QString& wanted,
                         int timeoutMs = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        const auto snapshot = client.request("snapshot");
        const auto job = jobFromSnapshot(snapshot, id);
        if (!job.isEmpty() && job.value("state").toString() == wanted) return job;
        QThread::msleep(50);
    }
    throw std::runtime_error((QStringLiteral("job did not reach state ") + wanted).toStdString());
}

QString socketName() {
    return QStringLiteral("campus-print-test-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QStringList coordinatorArgs(const QString& root, const QString& socket) {
    return {
        "--state-dir", root,
        "--socket-name", socket,
        "--page-ms", "20",
        "--max-restarts", "1",
        "--timeout-ms", "20000"
    };
}

void startCoordinator(QProcess& process, const QString& executable,
                      const QString& root, const QString& socket) {
    process.setProgram(executable);
    process.setArguments(coordinatorArgs(root, socket));
    process.start();
    require(process.waitForStarted(5000), "coordinator did not start");
}

QJsonObject submit(Client& client, const QString& id, int pages) {
    return client.request("submit", {
        {"job", QJsonObject{
            {"job_id", id},
            {"title", QStringLiteral("B组集成测试")},
            {"pages", pages},
            {"copies", 1},
            {"priority", 5},
            {"max_attempts", 3}
        }}
    });
}

QJsonObject submitWithAttempts(Client& client, const QString& id, int pages, int maxAttempts) {
    return client.request("submit", {
        {"job", QJsonObject{
            {"job_id", id},
            {"title", QStringLiteral("B组重试测试")},
            {"pages", pages},
            {"copies", 1},
            {"priority", 5},
            {"max_attempts", maxAttempts}
        }}
    });
}

void shutdown(Client& client, QProcess& process) {
    const auto response = client.request("shutdown");
    require(response.value("type").toString() == QStringLiteral("accepted"), "shutdown rejected");
    require(process.waitForFinished(10000), "coordinator did not shut down");
    require(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            "coordinator shutdown failed");
}

#ifdef Q_OS_WIN
void incompleteRecoveryLocks(const QString& executable) {
    QTemporaryDir temporary;
    require(temporary.isValid(), "incomplete-lock temporary directory unavailable");
    const QString root = temporary.path();
    const QDir output(QDir(root).filePath("output"));
    require(QDir().mkpath(output.path()), "cannot create incomplete-lock output directory");

    // Reproduce the interval after QLockFile's CREATE_NEW but before its PID
    // write, using exactly Qt 6.10.2's Windows access and sharing flags.
    struct IncompleteLock {
        HANDLE handle;
        explicit IncompleteLock(const QString& path)
            : handle(CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
                GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
        ~IncompleteLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        bool release() {
            if (handle == INVALID_HANDLE_VALUE || !CloseHandle(handle)) return false;
            handle = INVALID_HANDLE_VALUE;
            return true;
        }
        IncompleteLock(const IncompleteLock&) = delete;
        IncompleteLock& operator=(const IncompleteLock&) = delete;
    };
    const QString supervisorPath = output.filePath(".held-supervisor.supervisor.lock");
    const QString writerPath = output.filePath(".held-writer.output.lock");
    IncompleteLock supervisorLock(supervisorPath), writerLock(writerPath);
    require(supervisorLock.handle != INVALID_HANDLE_VALUE && writerLock.handle != INVALID_HANDLE_VALUE,
            "cannot hold incomplete native locks");
    for (const auto& path : {supervisorPath, writerPath}) {
        QLockFile probe(path);
        probe.setStaleLockTime(0);
        qint64 pid = 0;
        QString hostname, appname;
        require(!probe.tryLock(0) && probe.error() == QLockFile::LockFailedError &&
                !probe.getLockInfo(&pid, &hostname, &appname), "incomplete lock fixture is not incomplete");
        require(!probe.removeStaleLockFile(), "native removal stole a held incomplete lock");
    }

    const double submitted = static_cast<double>(QDateTime::currentMSecsSinceEpoch() - 10);
    const auto cancellingJob = [&](const QString& id) {
        return QJsonObject{{"job_id", id}, {"title", id}, {"pages", 2}, {"copies", 1},
            {"submitted_ms", submitted}, {"state", "cancelling"}, {"priority", 0},
            {"max_attempts", 3}, {"attempts", 1}, {"queued_since_ms", submitted},
            {"accumulated_wait_ms", 1}, {"last_changed_ms", submitted + 2},
            {"first_started_ms", submitted + 1}, {"finished_ms", QJsonValue(QJsonValue::Null)},
            {"runs", QJsonArray{QJsonObject{{"started_ms", submitted + 1},
                {"stopped_ms", QJsonValue(QJsonValue::Null)}}}}};
    };
    auto queued = cancellingJob("after-lock-release");
    queued.insert("state", "queued"); queued.insert("attempts", 0);
    queued.insert("accumulated_wait_ms", 0); queued.insert("last_changed_ms", submitted);
    queued.insert("first_started_ms", QJsonValue(QJsonValue::Null)); queued.insert("runs", QJsonArray{});
    const auto state = QJsonDocument(QJsonObject{{"schema_version", 1},
        {"kind", "campus-print-state"}, {"saved_at_ms", submitted + 2}, {"policy", "fcfs"},
        {"printers", 2}, {"aging_interval_ms", 30000}, {"jobs", QJsonArray{
            cancellingJob("held-supervisor"), cancellingJob("held-writer"), queued}}}).toJson();
    QFile stateFile(QDir(root).filePath("state.json"));
    require(stateFile.open(QIODevice::WriteOnly) && stateFile.write(state) == state.size(),
            "cannot write incomplete-lock recovery state");
    stateFile.close();

    QProcess coordinator;
    QJsonObject lastSnapshot;
    try {
        const auto socket = socketName();
        startCoordinator(coordinator, executable, root, socket);
        Client client;
        client.connectTo(socket);
        QElapsedTimer held; held.start();
        do {
            lastSnapshot = client.request("snapshot");
            for (const auto* id : {"held-supervisor", "held-writer"}) {
                const auto job = jobFromSnapshot(lastSnapshot, id);
                require(job.value("state") == "cancelling" && job.value("recovering").toBool() &&
                        job.value("attempts").toInt() == 1 && job.value("finished_ms").isNull(),
                        "held incomplete lock allowed recovery to settle or retry");
                require(!QFileInfo::exists(output.filePath(QString(id) + ".txt")) &&
                        !QFileInfo::exists(output.filePath(QString(id) + ".events.jsonl")),
                        "held incomplete lock allowed an executor to start");
            }
            const auto waiting = jobFromSnapshot(lastSnapshot, "after-lock-release");
            require(waiting.value("state") == "queued" && waiting.value("attempts").toInt() == 0,
                    "held recovery locks released a printer slot");
            QThread::msleep(50);
        } while (held.elapsed() < 1000); // Cover more than three 200 ms recovery passes.
        for (const auto& path : {supervisorPath, writerPath})
            require(QFileInfo::exists(path) && QFileInfo(path).size() == 0,
                    "held incomplete lock was removed or rewritten");
        require(supervisorLock.release() && writerLock.release(), "cannot release native lock handles");
        // The same empty files now have no OS owner. No age expiry is involved.
        for (const auto* id : {"held-supervisor", "held-writer"}) {
            const auto job = waitForState(client, id, "cancelled");
            require(job.value("attempts").toInt() == 1 && !job.value("finished_ms").isNull() &&
                    !job.value("recovering").toBool(), "orphan lock recovery did not settle the original attempt");
            require(!QFileInfo::exists(output.filePath(QString(id) + ".txt")),
                    "cancelled orphan-lock job exposed output");
        }
        waitForState(client, "after-lock-release", "completed");
        shutdown(client, coordinator);
        require(!QFileInfo::exists(supervisorPath) && !QFileInfo::exists(writerPath),
                "recovered orphan lock files survived");
        require(QDir(QDir(root).filePath("control")).entryList(QDir::Files).isEmpty(),
                "recovered orphan locks left cancellation markers");
    } catch (...) {
        std::cerr << "Incomplete-lock recovery snapshot: "
                  << QJsonDocument(lastSnapshot).toJson(QJsonDocument::Compact).constData() << '\n';
        for (const auto& path : {supervisorPath, writerPath}) {
            QFile file(path);
            std::cerr << path.toStdString() << " exists=" << QFileInfo::exists(path)
                      << " size=" << QFileInfo(path).size();
            if (file.open(QIODevice::ReadOnly)) std::cerr << " bytes=" << file.read(1024).toHex().constData();
            std::cerr << '\n';
        }
        std::cerr << "Cancellation markers: " << QDir(QDir(root).filePath("control"))
            .entryList(QDir::Files).join(", ").toStdString() << '\n'
            << coordinator.readAllStandardError().constData();
        throw;
    }
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    try {
        const QStringList arguments = app.arguments();
        const int flag = arguments.indexOf("--coordinator");
        require(flag >= 0 && flag + 1 < arguments.size(), "--coordinator is required");
        const QString executable = QFileInfo(arguments[flag + 1]).absoluteFilePath();

        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory unavailable");
        const QString root = QDir(temporary.path()).filePath(QStringLiteral("中文 状态目录"));
        require(QDir().mkpath(root), "cannot create state directory");

        QString socket = socketName();
        QProcess coordinator;
        startCoordinator(coordinator, executable, root, socket);
        Client client;
        client.connectTo(socket);

        QProcess duplicate;
        duplicate.setProgram(executable);
        duplicate.setArguments(coordinatorArgs(root, socketName()));
        duplicate.start();
        require(duplicate.waitForFinished(5000), "duplicate coordinator did not exit");
        require(duplicate.exitCode() == 75, "duplicate coordinator must fail with lock exit code 75");

        const QString otherRoot = QDir(temporary.path()).filePath("other-state");
        require(QDir().mkpath(otherRoot), "cannot create second state directory");
        QProcess socketCollision;
        socketCollision.setProgram(executable);
        socketCollision.setArguments(coordinatorArgs(otherRoot, socket));
        socketCollision.start();
        require(socketCollision.waitForFinished(5000), "socket collision coordinator did not exit");
        require(socketCollision.exitCode() == 75, "active socket name must not be stolen");

        Client stillConnected;
        stillConnected.connectTo(socket);
        require(stillConnected.request("snapshot").value("type").toString() == QStringLiteral("snapshot"),
                "primary coordinator socket was disrupted");
        for (const auto* name : {"fcfs", "sjf", "balanced", "priority", "deadline", "revenue"}) {
            const QString requested = QString::fromLatin1(name);
            const QString canonical = requested == "fcfs" || requested == "sjf" ? requested : QStringLiteral("balanced");
            const auto response = client.request("set_policy", {{"policy", requested}});
            require(response.value("type") == "accepted" && response.value("policy") == canonical,
                    "IPC accepts three canonical policies and migrates retired names");
            require(client.request("snapshot").value("policy") == canonical, "snapshot uses canonical policy");
            QFile savedPolicy(QDir(root).filePath("state.json"));
            require(savedPolicy.open(QIODevice::ReadOnly), "open persisted policy");
            require(QJsonDocument::fromJson(savedPolicy.readAll()).object().value("policy") == canonical,
                    "policy migration is persisted before acknowledgement");
        }
        require(client.request("set_policy", {{"policy", "typo"}}).value("type") == "error" &&
                client.request("snapshot").value("policy") == "balanced", "invalid policy cannot mutate the state");
        require(client.request("set_policy", {{"policy", "fcfs"}}).value("type") == "accepted", "restore baseline policy");

        const auto accepted = submit(client, "normal-job", 4);
        require(accepted.value("type").toString() == QStringLiteral("accepted"), "submit rejected");
        waitForState(client, "normal-job", "completed");
        const QString normalOutput = QDir(root).filePath("output/normal-job.txt");
        const QString normalEvents = QDir(root).filePath("output/normal-job.events.jsonl");
        require(QFileInfo(normalOutput).isFile(), "normal output missing");
        require(QFileInfo(normalEvents).isFile(), "normal event log missing");
        const qint64 completedEventBytes = QFileInfo(normalEvents).size();
        shutdown(client, coordinator);

        socket = socketName();
        // Leftovers from a crashed generation must be swept at startup.
        {
            QFile ghostDescriptor(QDir(root).filePath("jobs/ghost.json"));
            require(ghostDescriptor.open(QIODevice::WriteOnly), "cannot plant ghost descriptor");
            ghostDescriptor.write("{}");
            ghostDescriptor.close();
            QFile ghostMarker(QDir(root).filePath("control/ghost.1.cancel"));
            require(ghostMarker.open(QIODevice::WriteOnly), "cannot plant ghost marker");
            ghostMarker.close();
        }
        startCoordinator(coordinator, executable, root, socket);
        Client restarted;
        restarted.connectTo(socket);
        const auto restored = jobFromSnapshot(restarted.request("snapshot"), "normal-job");
        require(restored.value("state").toString() == QStringLiteral("completed"),
                "completed state did not survive restart");
        QThread::msleep(250);
        require(QFileInfo(normalEvents).size() == completedEventBytes,
                "completed job was executed again after restart");
        require(!QFileInfo::exists(QDir(root).filePath("jobs/ghost.json")),
                "stale job descriptor survived startup garbage collection");
        require(!QFileInfo::exists(QDir(root).filePath("control/ghost.1.cancel")),
                "stale cancel marker survived startup garbage collection");

        require(QDir().mkpath(QDir(root).filePath("output/exhaust-job.txt")),
                "cannot create retry failure fixture");
        const auto retryAccepted = submitWithAttempts(restarted, "exhaust-job", 2, 2);
        require(retryAccepted.value("type").toString() == QStringLiteral("accepted"),
                "retry exhaustion job rejected");
        const auto exhausted = waitForState(restarted, "exhaust-job", "failed", 15000);
        require(exhausted.value("attempts").toInt() == 2,
                "scheduler attempt limit was not enforced");

        submit(restarted, "cancel-job", 200);
        waitForState(restarted, "cancel-job", "running");
        const auto cancel = restarted.request("cancel", {{"job_id", "cancel-job"}});
        require(cancel.value("type").toString() == QStringLiteral("accepted"), "cancel rejected");
        waitForState(restarted, "cancel-job", "cancelled");
        require(!QFileInfo(QDir(root).filePath("output/cancel-job.txt")).exists(),
                "cancelled job exposed final output");

        const auto stats = restarted.request("stats");
        require(stats.value("type").toString() == QStringLiteral("stats"), "stats command unsupported");
        const auto counters = stats.value("statistics").toObject();
        require(counters.value("completed").toInt() >= 1 &&
                counters.value("failed").toInt() >= 1 &&
                counters.value("cancelled").toInt() >= 1,
                "stats counters do not reflect finished jobs");
        const auto badWindow = restarted.request("stats", {{"window_start_ms", 10}, {"window_end_ms", 5}});
        require(badWindow.value("type").toString() == QStringLiteral("error"),
                "stats accepted an inverted window");
        const auto negativeWindow = restarted.request("stats", {{"window_start_ms", -1}});
        require(negativeWindow.value("type").toString() == QStringLiteral("error"),
                "stats accepted a negative window start");

        submit(restarted, "recovery-job", 80);
        waitForState(restarted, "recovery-job", "running");
        coordinator.kill();
        require(coordinator.waitForFinished(5000), "failed to kill coordinator for recovery test");

        socket = socketName();
        QProcess recoveredCoordinator;
        startCoordinator(recoveredCoordinator, executable, root, socket);
        Client recovered;
        recovered.connectTo(socket);
        waitForState(recovered, "recovery-job", "completed", 15000);
        require(QFileInfo(QDir(root).filePath("output/recovery-job.txt")).isFile(),
                "recovered job output missing");
        shutdown(recovered, recoveredCoordinator);

        // Every job above reached a terminal state, so no descriptor or
        // cancellation marker may remain behind.
        require(QDir(QDir(root).filePath("jobs")).entryList(QDir::Files).isEmpty(),
                "terminal jobs left descriptor files behind");
        require(QDir(QDir(root).filePath("control")).entryList(QDir::Files).isEmpty(),
                "terminal jobs left cancellation markers behind");

        QTemporaryDir blockedTemporary;
        require(blockedTemporary.isValid(), "blocked-output temporary directory unavailable");
        const QString blockedRoot = QDir(blockedTemporary.path()).filePath("blocked-state");
        require(QDir().mkpath(blockedRoot), "cannot create blocked state directory");
        QFile outputBlocker(QDir(blockedRoot).filePath("output"));
        require(outputBlocker.open(QIODevice::WriteOnly), "cannot create output blocker");
        outputBlocker.close();
        QProcess blockedCoordinator;
        blockedCoordinator.setProgram(executable);
        blockedCoordinator.setArguments(coordinatorArgs(blockedRoot, socketName()));
        blockedCoordinator.start();
        require(blockedCoordinator.waitForFinished(5000), "blocked-output coordinator did not exit");
        require(blockedCoordinator.exitCode() == 73,
                "unusable output directory must fail coordinator startup with 73");

#ifdef Q_OS_WIN
        incompleteRecoveryLocks(executable);

        QProcess daemonRejected;
        daemonRejected.setProgram(executable);
        auto daemonArgs = coordinatorArgs(root, socketName());
        daemonArgs.append("--daemon");
        daemonRejected.setArguments(daemonArgs);
        daemonRejected.start();
        require(daemonRejected.waitForFinished(5000), "Windows daemon rejection did not exit");
        require(daemonRejected.exitCode() == 69, "Windows daemon mode must return 69");
#else
        QTemporaryDir daemonTemporary;
        require(daemonTemporary.isValid(), "daemon temporary directory unavailable");
        const QString daemonRoot = QDir(daemonTemporary.path()).filePath("daemon-state");
        require(QDir().mkpath(daemonRoot), "cannot create daemon state directory");
        const QString daemonSocket = socketName();
        QProcess daemonLauncher;
        daemonLauncher.setProgram(executable);
        auto daemonArgs = coordinatorArgs(QFileInfo(daemonRoot).absoluteFilePath(), daemonSocket);
        daemonArgs.append("--daemon");
        daemonLauncher.setArguments(daemonArgs);
        daemonLauncher.start();
        require(daemonLauncher.waitForFinished(5000), "daemon launcher did not detach");
        require(daemonLauncher.exitCode() == 0, "daemon launcher failed");
        Client daemonClient;
        daemonClient.connectTo(daemonSocket);
        require(daemonClient.request("snapshot").value("type").toString() == QStringLiteral("snapshot"),
                "daemon coordinator did not accept IPC");
        require(daemonClient.request("shutdown").value("type").toString() == QStringLiteral("accepted"),
                "daemon shutdown rejected");
        require(daemonClient.waitForDisconnected(), "daemon coordinator did not exit after shutdown");
#endif

        std::cout << "Coordinator persistence, locking, cancellation and crash recovery passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
