#include "cps/platform/daemon.hpp"
#include "cps/platform/stdio.hpp"
#include "cps/runtime/job_io.hpp"

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
#include <cstdio>
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
        {"daemon", "Detach from terminal on POSIX; unsupported on Windows"}});
    parser.process(app);
    cps::Job job;
    QString error;
    bool validRestarts = false;
    bool validTimeout = false;
    bool validDelay = false;
    const int maxRestarts = parser.value("max-restarts").toInt(&validRestarts);
    const int timeout = parser.value("timeout-ms").toInt(&validTimeout);
    const int pageMs = parser.value("page-ms").toInt(&validDelay);
    if (!validRestarts || maxRestarts < 0 || maxRestarts > 10 ||
        !validTimeout || timeout < 20000 || !validDelay || pageMs < 10 || pageMs > 10000 ||
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
    worker.setArguments({"--job", jobPath, "--output-dir", output.absolutePath(),
                         "--page-ms", QString::number(pageMs)});
    QTimer watchdog;
    watchdog.setSingleShot(true);
    int restarts = 0;
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&] {
        event({{"type", "worker_timeout"}});
        worker.kill();
    });
    QObject::connect(&worker, &QProcess::started, &app, [&] {
        event({{"type", "worker_started"}, {"pid", static_cast<double>(worker.processId())},
               {"attempt", restarts + 1}});
        watchdog.start(timeout);
    });
    QObject::connect(&worker, &QProcess::readyReadStandardOutput, &app, [&] {
        const auto bytes = worker.readAllStandardOutput();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stdout);
        std::fflush(stdout);
        watchdog.start(timeout);
    });
    QObject::connect(&worker, &QProcess::readyReadStandardError, &app, [&] {
        const auto bytes = worker.readAllStandardError();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stderr);
    });
    QObject::connect(&worker, &QProcess::errorOccurred, &app, [&](QProcess::ProcessError code) {
        if (code == QProcess::FailedToStart) {
            event({{"type", "failed"}, {"reason", worker.errorString()}});
            app.exit(78);
        }
    });
    QObject::connect(&worker, &QProcess::finished, &app, [&](int code, QProcess::ExitStatus status) {
        watchdog.stop();
        if (status == QProcess::NormalExit && code == 0) {
            event({{"type", "supervisor_completed"}, {"restarts", restarts}});
            app.quit();
        } else if (restarts < maxRestarts && !(status == QProcess::NormalExit && code == 64)) {
            ++restarts;
            event({{"type", "worker_restarting"}, {"restart", restarts}, {"exit_code", code}});
            QTimer::singleShot(200 * restarts, &worker, [&] { worker.start(); });
        } else {
            event({{"type", "failed"}, {"exit_code", code}, {"restarts", restarts}});
            app.exit(70);
        }
    });
    worker.start();
    return app.exec();
}
