#include "print_controller.hpp"
#include "cps/runtime/job_io.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QUuid>
#include <cmath>

namespace cps::gui {
PrintController::PrintController(WindowOptions options) : options_(std::move(options)) {}

void PrintController::initialize() {
    Q_ASSERT(QThread::currentThread() == thread());
    startedMs_ = scheduler_.nowMs();
    auto* refresh = new QTimer(this);
    connect(refresh, &QTimer::timeout, this, &PrintController::publish);
    refresh->start(100);
    checkConnection();
}

bool PrintController::addJob(const QString& title, int pages, int copies, int priority) {
    if (title.trimmed().isEmpty() || title.trimmed().size() > 200 || pages < 1 || pages > 1000 ||
        copies < 1 || copies > 100 || pages * copies > 10000 || priority < 0 || priority > 100)
        return false;
    Job job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    job.title = title.trimmed().toUtf8().toStdString();
    job.pages = pages;
    job.copies = copies;
    job.priority = priority;
    job.submittedMs = scheduler_.nowMs();
    if (!scheduler_.submit(job)) return false;
    const auto id = QString::fromStdString(job.id);
    details_.insert(id, {id, title.trimmed(), job.sheets(), priority, JobState::Queued, 0, {}});
    return true;
}

void PrintController::submit(const QString& title, int pages, int copies, int priority) {
    if (details_.size() >= 10000) {
        emit feedback(QStringLiteral("当前会话最多保留 10000 个任务；请导出报表后开启新会话。"));
    } else if (!addJob(title, pages, copies, priority)) {
        emit feedback(QStringLiteral("提交失败：名称须为 1–200 字符，总页数不得超过 10000。"));
    } else {
        emit feedback(QStringLiteral("任务已加入等待队列。"));
        dispatch();
    }
}

void PrintController::importFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        emit feedback(QStringLiteral("导入失败：文件无法读取或超过 4 MiB。"));
        return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray() || document.array().isEmpty() ||
        document.array().size() + details_.size() > 10000) {
        emit feedback(QStringLiteral("导入失败：需要非空 JSON 数组，当前会话总数不得超过 10000。"));
        return;
    }
    const auto items = document.array();
    // Validate the entire batch before submission so a malformed row cannot partially import.
    for (const auto& item : items) {
        const auto value = item.toObject();
        auto validNumber = [&value](const char* key, int minimum, int maximum, int fallback) {
            const auto v = value.value(QLatin1String(key));
            const double n = v.isUndefined() ? fallback : v.toDouble(-1);
            return (v.isUndefined() || v.isDouble()) && n >= minimum && n <= maximum && std::floor(n) == n;
        };
        const auto title = value.value("title").toString().trimmed();
        if (!item.isObject() || title.isEmpty() || title.size() > 200 ||
            !validNumber("pages", 1, 1000, -1) || !validNumber("copies", 1, 100, 1) ||
            !validNumber("priority", 0, 100, 0) ||
            value.value("pages").toInt() * value.value("copies").toInt(1) > 10000) {
            emit feedback(QStringLiteral("导入失败：存在无效的名称、页数、份数或优先级；未导入任何任务。"));
            return;
        }
    }
    for (const auto& item : items) {
        const auto value = item.toObject();
        addJob(value.value("title").toString(), value.value("pages").toInt(),
               value.value("copies").toInt(1), value.value("priority").toInt());
    }
    emit feedback(QStringLiteral("已导入 %1 个模拟任务（立即入队，生成新任务 ID）。").arg(items.size()));
    dispatch();
}

void PrintController::cancel(const QString& id) {
    // v1 supervisor has no cancellation/stop acknowledgement command.
    // Never kill a supervisor and falsely acknowledge that its worker stopped.
    if (!details_.contains(id) || details_[id].state != JobState::Queued) {
        emit feedback(QStringLiteral("当前执行器尚不支持运行中取消；等待 B 提供停止确认接口。"));
        return;
    }
    scheduler_.cancel(id.toStdString(), scheduler_.nowMs());
    publish();
}

void PrintController::setPolicy(int index) {
    if (index < 0 || index > 2) return;
    policy_ = index == 0 ? Policy::Fcfs : index == 1 ? Policy::ShortestJobFirst : Policy::PriorityAging;
    dispatch();
}

void PrintController::setPaused(bool paused) {
    paused_ = paused;
    dispatch();
    publish();
}

void PrintController::checkConnection() {
    available_ = QFileInfo(cps::siblingExecutable("print-supervisor")).isExecutable() &&
        QFileInfo(cps::siblingExecutable("print-worker")).isExecutable();
    if (!available_) emit feedback(QStringLiteral("执行器不可用：请检查同目录的 supervisor、worker 及 Qt 运行库。"));
    dispatch();
    publish();
}

void PrintController::dispatch() {
    if (paused_ || !available_) return;
    for (int slot = 0; slot < 2; ++slot) {
        if (runs_[slot]) continue;
        const auto job = scheduler_.claim(policy_);
        if (!job) break;
        const QString id = QString::fromStdString(job->id);
        details_[id].state = JobState::Running;
        const QString jobsDirectory = QDir(options_.stateDirectory).filePath("jobs");
        const QString path = QDir(jobsDirectory).filePath(id + ".json");
        QString error;
        if (!QDir().mkpath(jobsDirectory) || !cps::writeJob(path, *job, error)) {
            scheduler_.finish(job->id, job->attempts, false, scheduler_.nowMs());
            details_[id].detail = QStringLiteral("写入任务失败：") + error;
            log(details_[id].detail);
            // Yield between failures rather than drain a large queue in one event callback.
            QTimer::singleShot(0, this, &PrintController::dispatch);
            continue;
        }
        auto* process = new QProcess(this);
        runs_[slot] = std::make_unique<Run>(*job, process);
        printers_[slot] = {id, details_[id].title, QStringLiteral("正在启动"), 0, job->sheets()};
        process->setProgram(cps::siblingExecutable("print-supervisor"));
        process->setArguments({"--job", path, "--output-dir", QDir(options_.stateDirectory).filePath("output"),
            "--page-ms", QString::number(options_.pageMs)});
        connect(process, &QProcess::readyReadStandardOutput, this, [this, slot] {
            auto& run = *runs_[slot];
            receive(slot, run.stream.append(run.process->readAllStandardOutput()));
        });
        connect(process, &QProcess::readyReadStandardError, this, [this, slot] {
            auto& run = *runs_[slot];
            const auto errorText = QString::fromUtf8(run.process->readAllStandardError()).trimmed().left(2000);
            if (!errorText.isEmpty()) {
                run.detail = errorText;
                log(errorText);
            }
        });
        connect(process, &QProcess::errorOccurred, this, [this, slot](QProcess::ProcessError code) {
            if (code == QProcess::FailedToStart && runs_[slot]) {
                available_ = false;
                runs_[slot]->detail = runs_[slot]->process->errorString();
                complete(slot, 78, QProcess::CrashExit);
            }
        });
        connect(process, &QProcess::finished, this, [this, slot](int code, QProcess::ExitStatus status) {
            complete(slot, code, status);
        });
        process->start();
    }
}

void PrintController::receive(int slot, const EventBatch& batch) {
    auto& run = *runs_[slot];
    auto& printer = printers_[slot];
    auto& detail = details_[printer.jobId];
    for (const auto& error : batch.errors) log(printer.jobId + ": " + error);
    for (const auto& event : batch.events) {
        const auto type = event.value("type").toString();
        if (type == "progress") {
            if (event.value("total_sheets").toInt() != run.job.sheets()) {
                log(QStringLiteral("忽略总页数与任务不一致的进度：") + printer.jobId);
                continue;
            }
            printer.printed = event.value("printed_sheets").toInt();
            detail.printed = printer.printed;
            printer.status = QStringLiteral("打印中");
        } else if (type == "worker_started") {
            printer.printed = detail.printed = 0;
            run.outputCommitted = false;
            printer.status = QStringLiteral("打印中 · 第 %1 次执行").arg(event.value("attempt").toInt());
        } else if (type == "worker_restarting") {
            printer.printed = detail.printed = 0;
            run.outputCommitted = false;
            printer.status = QStringLiteral("故障恢复 · 第 %1 次重试").arg(event.value("restart").toInt());
        } else if (type == "worker_timeout") {
            printer.status = QStringLiteral("工作进程超时，等待恢复");
        } else if (type == "completed") {
            const auto expected = QDir(options_.stateDirectory).absoluteFilePath("output/" + printer.jobId + ".txt");
            run.outputCommitted = QFileInfo(event.value("output_path").toString()).absoluteFilePath() == expected;
            printer.status = QStringLiteral("已输出，等待监督进程确认");
        } else if (type == "supervisor_completed") {
            run.supervisorCompleted = true;
        } else if (type == "failed") {
            run.detail = event.value("reason").toString(QStringLiteral("执行失败，退出码 %1").arg(event.value("exit_code").toDouble()));
        }
        if (type != "progress") log(printer.jobId + ": " + QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
    }
}

void PrintController::complete(int slot, int exitCode, QProcess::ExitStatus status) {
    if (!runs_[slot]) return;
    auto& run = *runs_[slot];
    receive(slot, run.stream.append(run.process->readAllStandardOutput()));
    receive(slot, run.stream.finish());
    const auto id = QString::fromStdString(run.job.id);
    const QFileInfo output(QDir(options_.stateDirectory).filePath("output/" + id + ".txt"));
    const bool success = status == QProcess::NormalExit && exitCode == 0 &&
        run.outputCommitted && run.supervisorCompleted && output.isFile() && output.size() > 0;
    scheduler_.finish(run.job.id, run.job.attempts, success, scheduler_.nowMs());
    details_[id].detail = success ? QStringLiteral("原子输出已确认") :
        (run.detail.isEmpty() ? QStringLiteral("执行未成功确认（退出码 %1）；请查看日志。").arg(exitCode) : run.detail);
    if (success) details_[id].printed = run.job.sheets();
    log(id + ": " + details_[id].detail);
    run.process->disconnect(this);
    run.process->deleteLater();
    runs_[slot].reset();
    printers_[slot] = {};
    publish();
    QTimer::singleShot(0, this, &PrintController::dispatch);
}

void PrintController::publish() {
    const auto jobs = scheduler_.snapshot();
    const auto now = scheduler_.nowMs();
    ViewState view;
    view.jobs.reserve(static_cast<qsizetype>(jobs.size()));
    for (const auto& job : jobs) {
        auto& detail = details_[QString::fromStdString(job.id)];
        detail.state = job.state;
        view.jobs.append(detail);
    }
    view.printers = printers_;
    view.paused = paused_;
    view.connection = available_ ? QStringLiteral("本地执行器就绪 · 内存队列") : QStringLiteral("执行器不可用 · 等待重新检查");
    view.metrics = statistics(jobs, startedMs_, now, 2);
    emit viewChanged(view);
    if (!logs_.isEmpty()) {
        emit logsReady(logs_);
        logs_.clear();
    }
}

void PrintController::log(const QString& line) {
    if (logs_.size() >= 200) logs_.removeFirst();
    logs_.append(line.left(2000));
}

void PrintController::exportReport(const QString& path) {
    const auto jobs = scheduler_.snapshot();
    const auto now = scheduler_.nowMs();
    const auto stats = statistics(jobs, startedMs_, now, 2);
    QJsonArray rows;
    for (const auto& job : jobs) {
        rows.append(QJsonObject{{"job_id", QString::fromStdString(job.id)},
            {"title", QString::fromStdString(job.title)}, {"sheets", job.sheets()},
            {"state", static_cast<int>(job.state)}, {"priority", job.priority}});
    }
    const QJsonObject report{{"schema_version", 1}, {"mode", "local_simulation"},
        {"policy", policy_ == Policy::Fcfs ? "FCFS" : policy_ == Policy::ShortestJobFirst ? "SJF" : "PriorityAging"},
        {"policy_note", "Policy can change during a session; this is not a controlled policy comparison."},
        {"window_start_ms", static_cast<double>(startedMs_)}, {"window_end_ms", static_cast<double>(now)},
        {"printers", 2}, {"page_ms", options_.pageMs}, {"completed", static_cast<double>(stats.completed)},
        {"failed", static_cast<double>(stats.failed)}, {"cancelled", static_cast<double>(stats.cancelled)},
        {"average_first_wait_ms", stats.averageFirstWaitMs}, {"average_turnaround_ms", stats.averageTurnaroundMs},
        {"throughput_per_second", stats.throughputPerSecond}, {"utilization", stats.utilization}, {"jobs", rows}};
    const auto data = QJsonDocument(report).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        emit feedback(QStringLiteral("报表写入失败：") + file.errorString());
    else emit feedback(QStringLiteral("报表已保存：") + path);
}

void PrintController::requestClose() {
    const auto jobs = scheduler_.snapshot();
    bool busy = false;
    for (const auto& job : jobs)
        busy = busy || job.state == JobState::Queued || job.state == JobState::Running || job.state == JobState::Cancelling;
    if (busy) emit feedback(QStringLiteral("仍有等待或运行任务：请继续派发至完成，或取消等待任务后退出。"));
    emit closeReady(!busy);
}
} // namespace cps::gui
