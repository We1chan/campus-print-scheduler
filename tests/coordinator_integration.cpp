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
