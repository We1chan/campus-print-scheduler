#include "cps/platform/daemon.hpp"
#include "cps/platform/stdio.hpp"
#include "cps/runtime/job_io.hpp"
#include "control_server.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <cstdio>
#include <memory>
#include <vector>

int main(int argc, char* argv[]) {
    // Fork before constructing any Qt application or starting an event loop.
    bool detached = false;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--daemon") detached = true;
    }
    if (detached) {
#ifdef Q_OS_WIN
        std::fprintf(stderr, "--daemon is POSIX-only; use foreground supervision on Windows.\n");
        return 69;
#else
        bool absoluteJob = false;
        bool absoluteOutput = false;
        for (int i = 1; i + 1 < argc; ++i) {
            const QString option = QString::fromLocal8Bit(argv[i]);
            const QFileInfo path(QString::fromLocal8Bit(argv[i + 1]));
            if (option == "--job") absoluteJob = path.isAbsolute() && path.isFile();
            if (option == "--output-dir") absoluteOutput = path.isAbsolute();
        }
        if (!absoluteJob || !absoluteOutput) {
            std::fprintf(stderr, "--daemon requires --job and --output-dir with absolute paths\n");
            return 64;
        }
        const auto executable = QStandardPaths::findExecutable(QString::fromLocal8Bit(argv[0])).toLocal8Bit();
        if (executable.isEmpty()) return 69;
        std::vector<char*> arguments;
        for (int i = 0; i < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) != "--daemon") arguments.push_back(argv[i]);
        }
        arguments.push_back(nullptr);
        QString error;
        if (!cps::detachDaemon(executable.constData(), arguments.data(), error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 69;
        }
#endif
    }
    cps::configureIpcStdio();
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("print-supervisor");
    QCommandLineParser parser;
    parser.setApplicationDescription("Watch one simulated print job and restart a crashed worker.");
    parser.addHelpOption();
    parser.addOptions({{"job", "Job JSON file", "file"},
        {"output-dir", "Output and event log directory", "directory"},
        {"page-ms", "Milliseconds per sheet (10..10000)", "ms", "100"},
        {"max-restarts", "Restart limit (0..10)", "count", "3"},
        {"timeout-ms", "Maximum silence from worker (>=20000)", "ms", "30000"},
        {"cancel-file", "Cancellation marker watched while the job is active", "file"},
        {"daemon", "Detach from terminal on POSIX; unsupported on Windows"},
        {"control-token", "Enable authenticated loopback control using this token", "token"},
        {"claim-attempt", "Scheduler claim token, independent of worker restarts", "attempt", "1"},
        {"lease-ms", "Control heartbeat lease (500..60000 ms)", "ms", "8000"},
        {"demo-mode", "Allow controlled worker crash injection"},
        {"demo-completion-delay-ms", "Demo-only hold after worker commits output (0..2000)", "ms", "0"}});
    parser.process(app);
    cps::Job job;
    QString error;
    bool validRestarts = false;
    bool validTimeout = false;
    bool validDelay = false;
    const int maxRestarts = parser.value("max-restarts").toInt(&validRestarts);
    const int timeout = parser.value("timeout-ms").toInt(&validTimeout);
    const int pageMs = parser.value("page-ms").toInt(&validDelay);
    bool validClaim = false;
    bool validLease = false;
    const int claimAttempt = parser.value("claim-attempt").toInt(&validClaim);
    const int leaseMs = parser.value("lease-ms").toInt(&validLease);
    const auto controlToken = parser.value("control-token");
    bool validCompletionDelay = false;
    const int completionDelay = parser.value("demo-completion-delay-ms").toInt(&validCompletionDelay);
    if (!validRestarts || maxRestarts < 0 || maxRestarts > 10 ||
        !validTimeout || timeout < 20000 || !validDelay || pageMs < 10 || pageMs > 10000 ||
        !validClaim || claimAttempt < 1 || claimAttempt > 100 || !validLease || leaseMs < 500 || leaseMs > 60000 ||
        !validCompletionDelay || completionDelay < 0 || completionDelay > 2000 || (completionDelay > 0 && !parser.isSet("demo-mode")) ||
        (!controlToken.isEmpty() && !QRegularExpression("^[A-Za-z0-9_-]{32,80}$").match(controlToken).hasMatch()) ||
        parser.value("output-dir").isEmpty() || !cps::readJob(parser.value("job"), job, error)) {
        std::fprintf(stderr, "Invalid supervisor options: %s\n", qPrintable(error));
        return 64;
    }
    const QString jobPath = QFileInfo(parser.value("job")).absoluteFilePath();
    QDir output(QFileInfo(parser.value("output-dir")).absoluteFilePath());
    if (!QDir().mkpath(output.absolutePath())) return 73;
    const QString id = QString::fromStdString(job.id);
    QLockFile lock(output.filePath("." + id + ".supervisor.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) return 75;
    QFile log(output.filePath(id + ".events.jsonl"));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append)) return 73;
    auto event = [&](QJsonObject object) {
        object.insert("schema_version", 1);
        object.insert("job_id", id);
        const auto line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (log.write(line) != line.size() || !log.flush()) {
            std::fprintf(stderr, "Cannot append supervisor event log\n");
        }
        cps::emitEvent(object);
    };
    QProcess worker;
    worker.setProgram(cps::siblingExecutable("print-worker"));
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QTimer restartTimer;
    restartTimer.setSingleShot(true);
    int restarts = 0;
    int printed = 0;
    bool cancelling = false;
    bool terminal = false;
    QString cancelReason;
    QString executionToken;
    QByteArray workerBuffer;
    std::unique_ptr<ControlServer> control;
    auto finish = [&](int code) {
        if (terminal) return;
        terminal = true;
        watchdog.stop();
        restartTimer.stop();
        if (control) control->stopLease();
        app.exit(code);
    };
    auto confirmCancel = [&] {
        event({{"type", "cancelled"}, {"claim_attempt", claimAttempt}, {"reason", cancelReason}});
        finish(130);
    };
    auto requestCancel = [&](const QString& reason) {
        if (terminal || cancelling) return;
        cancelling = true;
        cancelReason = reason;
        watchdog.stop();
        restartTimer.stop();
        event({{"type", "cancelling"}, {"claim_attempt", claimAttempt}});
        if (worker.state() == QProcess::NotRunning) confirmCancel();
        else worker.kill(); // Acknowledge only from finished, after the OS confirms it stopped.
    };
    auto startWorker = [&] {
        if (terminal || cancelling) return;
        printed = 0;
        workerBuffer.clear();
        executionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
        worker.setArguments({"--job", jobPath, "--output-dir", output.absolutePath(),
            "--page-ms", QString::number(pageMs), "--execution-token", executionToken,
            "--completion-delay-ms", QString::number(completionDelay)});
        worker.start();
    };
    auto drainWorker = [&] {
        const auto bytes = worker.readAllStandardOutput();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
        std::fflush(stdout);
        workerBuffer.append(bytes);
        qsizetype newline = -1;
        while ((newline = workerBuffer.indexOf('\n')) >= 0) {
            const auto object = QJsonDocument::fromJson(workerBuffer.left(newline)).object();
            workerBuffer.remove(0, newline + 1);
            if (object.value("job_id").toString() == id && object.value("type").toString() == "progress")
                printed = object.value("printed_sheets").toInt();
        }
        if (workerBuffer.size() > 64 * 1024) workerBuffer.clear();
    };
    if (!controlToken.isEmpty()) {
        control = std::make_unique<ControlServer>(id, controlToken, claimAttempt, leaseMs);
        control->snapshot = [&] {
            return QJsonObject{{"state", cancelling ? "cancelling" : "running"},
                {"printed_sheets", printed}, {"total_sheets", job.sheets()}, {"worker_attempt", restarts + 1},
                {"demo_mode", parser.isSet("demo-mode")}};
        };
        control->execute = [&](const QString& action, QString& reason) {
            if (terminal) { reason = "task already finished"; return false; }
            if (action == "cancel") { requestCancel("user cancellation"); return true; }
            if (action == "crash_worker" && parser.isSet("demo-mode") && !cancelling && worker.state() == QProcess::Running) {
                event({{"type", "fault_injected"}, {"claim_attempt", claimAttempt}, {"worker_attempt", restarts + 1}});
                worker.kill();
                return true;
            }
            reason = "unsupported command, demo disabled or worker not running";
            return false;
        };
        control->leaseExpired = [&] { requestCancel("control heartbeat lease expired"); };
        if (!control->listen()) return 73;
        event({{"type", "control_ready"}, {"port", control->port()}, {"claim_attempt", claimAttempt}});
    }
    QObject::connect(&restartTimer, &QTimer::timeout, &app, startWorker);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&] {
        event({{"type", "worker_timeout"}});
        worker.kill();
    });
    QObject::connect(&worker, &QProcess::started, &app, [&] {
        event({{"type", "worker_started"}, {"pid", static_cast<double>(worker.processId())},
               {"attempt", restarts + 1}});
        if (cancelling) worker.kill();
        else watchdog.start(timeout);
    });
    QObject::connect(&worker, &QProcess::readyReadStandardOutput, &app, [&] {
        drainWorker();
        if (!cancelling) watchdog.start(timeout);
    });
    QObject::connect(&worker, &QProcess::readyReadStandardError, &app, [&] {
        const auto bytes = worker.readAllStandardError();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stderr);
    });
    QObject::connect(&worker, &QProcess::errorOccurred, &app, [&](QProcess::ProcessError code) {
        if (code == QProcess::FailedToStart) {
            if (cancelling) { confirmCancel(); return; }
            event({{"type", "failed"}, {"reason", worker.errorString()}});
            finish(78);
        }
    });
    QObject::connect(&worker, &QProcess::finished, &app, [&](int code, QProcess::ExitStatus status) {
        drainWorker();
        watchdog.stop();
        // The receipt is atomically committed with the output. It identifies this
        // attempt even if cancellation raced commit before the completed event.
        QFile receipt(output.filePath(id + ".txt"));
        const bool committed = receipt.open(QIODevice::ReadOnly) &&
            receipt.readLine(128) == ("execution_token=" + executionToken + '\n').toUtf8();
        if ((status == QProcess::NormalExit && code == 0) || committed) {
            if (committed) event({{"type", "completed"}, {"output_path", output.absoluteFilePath(id + ".txt")}});
            event({{"type", "supervisor_completed"}, {"restarts", restarts}});
            finish(0);
        } else if (cancelling) {
            confirmCancel();
        } else if (restarts < maxRestarts && !(status == QProcess::NormalExit && code == 64)) {
            ++restarts;
            event({{"type", "worker_restarting"}, {"restart", restarts}, {"exit_code", code}});
            restartTimer.start(200 * restarts);
        } else {
            event({{"type", "failed"}, {"exit_code", code}, {"restarts", restarts}});
            finish(70);
        }
    });
    // The coordinator cancels by persisting an attempt-scoped marker file; the
    // control protocol stays the GUI's interactive path.
    const QString cancelFile = parser.value("cancel-file");
    if (!cancelFile.isEmpty()) {
        auto* cancelPoll = new QTimer(&app);
        cancelPoll->setInterval(100);
        QObject::connect(cancelPoll, &QTimer::timeout, &app, [&] {
            if (QFile::exists(cancelFile)) {
                requestCancel("cancel marker observed");
            }
        });
        cancelPoll->start();
    }
    startWorker();
    return app.exec();
}
