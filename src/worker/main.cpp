#include "cps/runtime/job_io.hpp"
#include "cps/platform/stdio.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QLockFile>
#include <QSaveFile>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>

int main(int argc, char* argv[]) {
    cps::configureIpcStdio();
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("print-worker");
    QCommandLineParser parser;
    parser.setApplicationDescription("Simulated printer: one JSON job, atomic text output, JSONL events.");
    parser.addHelpOption();
    parser.addOptions({{{"j", "job"}, "Job JSON file", "file"},
        {{"o", "output-dir"}, "Output directory", "directory"},
        {"page-ms", "Milliseconds per simulated sheet (10..24000)", "ms", "100"},
        {"execution-token", "Optional receipt identifying this execution", "token"},
        {"completion-delay-ms", "Hold after atomic commit for race demonstrations (0..2000)", "ms", "0"}});
    parser.process(app);
    cps::Job job;
    QString error;
    bool validDelay = false;
    const int pageMs = parser.value("page-ms").toInt(&validDelay);
    const QString executionToken = parser.value("execution-token");
    bool validCompletionDelay = false;
    const int completionDelay = parser.value("completion-delay-ms").toInt(&validCompletionDelay);
    if (!validDelay || pageMs < 10 || pageMs > 24000 || parser.value("output-dir").isEmpty() ||
        !validCompletionDelay || completionDelay < 0 || completionDelay > 2000 ||
        (!executionToken.isEmpty() && !QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(executionToken).hasMatch()) ||
        !cps::readJob(parser.value("job"), job, error)) {
        std::fprintf(stderr, "Invalid input: %s\n", qPrintable(error));
        return 64;
    }
    QDir output(parser.value("output-dir"));
    if (!QDir().mkpath(output.absolutePath())) return 73;
    const QString id = QString::fromStdString(job.id);
    QLockFile lock(output.filePath("." + id + ".output.lock"));
    lock.setStaleLockTime(0); // A long print job must never be mistaken for a stale lock.
    if (!lock.tryLock(0)) return 75;
    QSaveFile result(output.filePath(id + ".txt"));
    if (!result.open(QIODevice::WriteOnly)) return 73;
    if (!executionToken.isEmpty()) {
        const auto receipt = ("execution_token=" + executionToken + '\n').toUtf8();
        if (result.write(receipt) != receipt.size()) return 74;
    }
    const auto header = QString("job=%1\ntitle=%2\nsheets=%3\n")
        .arg(id, QString::fromUtf8(job.title.data(), static_cast<qsizetype>(job.title.size())))
        .arg(job.sheets()).toUtf8();
    if (result.write(header) != header.size()) return 74;
    const auto specifications = QString("paper=%1\ncolor=%2\nduplex=%3\nphysical_sheets=%4\n")
        .arg(job.paper == cps::PaperSize::A3 ? "a3" : "a4").arg(job.color).arg(job.duplex).arg(job.paperSheets()).toUtf8();
    if (result.write(specifications) != specifications.size()) return 74;
    int printed = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        ++printed;
        const auto line = QString("sheet=%1\n").arg(printed).toUtf8();
        if (result.write(line) != line.size()) { app.exit(74); return; }
        cps::emitEvent({{"schema_version", 1}, {"type", "progress"}, {"job_id", id},
                       {"printed_sheets", printed}, {"total_sheets", job.sheets()}});
        if (printed == job.sheets()) {
            timer.stop();
            if (!result.commit()) { app.exit(74); return; }
            cps::emitEvent({{"schema_version", 1}, {"type", "completed"}, {"job_id", id},
                           {"output_path", output.absoluteFilePath(id + ".txt")}});
            QTimer::singleShot(completionDelay, &app, &QCoreApplication::quit);
        }
    });
    timer.start(pageMs);
    return app.exec();
}
