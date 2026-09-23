#include "print_controller.hpp"
#include "cps/runtime/job_io.hpp"
#include "cps/runtime/state_store.hpp"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QThread>
#include <QUuid>
#include <cmath>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace cps::gui {
PrintController::PrintController(WindowOptions options) : options_(std::move(options)) {}

void PrintController::initialize() {
    Q_ASSERT(QThread::currentThread() == thread());
    socket_ = new QLocalSocket(this);
    socket_->setReadBufferSize(64 * 1024 * 1024 + 1);
    refresh_ = new QTimer(this);
    connect(refresh_, &QTimer::timeout, this, &PrintController::refresh);
    refresh_->start(300);
    refresh();
}

bool PrintController::connectBackend() {
    if (socket_->state() == QLocalSocket::ConnectedState) return true;
    if (!QDir().mkpath(options_.stateDirectory)) return false;
    const auto endpoint = cps::coordinatorSocketName(options_.stateDirectory);
    socket_->abort(); socket_->connectToServer(endpoint);
    if (socket_->waitForConnected(100)) return true;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastLaunchMs_ >= 3000) {
        lastLaunchMs_ = now;
        // Do not spawn repeatedly while an existing owner is starting or recovering.
        QLockFile probe(QDir(options_.stateDirectory).filePath(".coordinator.lock"));
        probe.setStaleLockTime(0);
        if (probe.tryLock(0)) {
            probe.unlock();
            QProcess backend;
            backend.setProgram(cps::siblingExecutable("print-coordinator"));
            QStringList args{"--state-dir", QDir(options_.stateDirectory).absolutePath(),
                "--page-ms", QString::number(options_.pageMs)};
            if (options_.demoMode) args << "--demo-mode";
            backend.setArguments(args);
            backend.setWorkingDirectory(QDir(options_.stateDirectory).absolutePath());
            backend.setStandardInputFile(QProcess::nullDevice());
            backend.setStandardOutputFile(QProcess::nullDevice());
            backend.setStandardErrorFile(QDir(options_.stateDirectory).filePath("coordinator.log"), QIODevice::Append);
#ifdef Q_OS_WIN
            // Qt's detached CreateProcess inherits all inheritable handles, including
            // a launcher's capture pipes even when we redirect the new standard streams.
            // Keep this GUI's own streams usable, but never pass them to the daemon.
            for (const DWORD channel : {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
                const HANDLE handle = GetStdHandle(channel);
                if (handle && handle != INVALID_HANDLE_VALUE &&
                    !SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0)) {
                    log(QStringLiteral("无法隔离后台启动句柄：%1").arg(GetLastError()));
                    return false;
                }
            }
#endif
            if (!backend.startDetached()) log(QStringLiteral("后台启动失败：") + backend.errorString());
        }
    }
    // Bounded waits happen only on this worker thread, never the QWidget thread.
    for (int i = 0; i < 10; ++i) {
        socket_->abort(); socket_->connectToServer(endpoint);
        if (socket_->waitForConnected(50)) return true;
        QThread::msleep(20);
    }
    return false;
}

void PrintController::disconnected() {
    if (available_) log(QStringLiteral("控制连接断开 · 正在重新连接持久后台"));
    available_ = false;
    socket_->abort();
}

QJsonObject PrintController::request(const QString& type, QJsonObject payload) {
    if (!connectBackend()) { disconnected(); return {}; }
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    payload.insert("schema_version", 1); payload.insert("request_id", id); payload.insert("type", type);
    const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > 8 * 1024 * 1024 || socket_->write(bytes) != bytes.size()) {
        disconnected(); return {};
    }
    QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 5000) {
        while (socket_->canReadLine()) {
            const auto line = socket_->readLine(64 * 1024 * 1024 + 1);
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject() ||
                doc.object().value("schema_version").toDouble() != 1) {
                disconnected(); return {};
            }
            const auto message = doc.object();
            if (message.value("request_id").toString() == id) return message;
            const auto event = message.value("type").toString();
            if (event != "progress" && event != "job_state" && event != "queue_changed")
                log(message.value("job_id").toString() + ": " + event);
        }
        if (socket_->bytesAvailable() >= 64 * 1024 * 1024 ||
            socket_->state() != QLocalSocket::ConnectedState) break;
        socket_->waitForReadyRead(100);
    }
    // Never replay a mutation after losing its acknowledgment: it may already be durable.
    disconnected(); return {};
}

void PrintController::mutate(const QString& type, QJsonObject payload) {
    const auto response = request(type, std::move(payload));
    if (response.value("type").toString() == "accepted")
        emit feedback(QStringLiteral("后台已确认操作并保存状态。"));
    else if (response.isEmpty())
        emit feedback(QStringLiteral("未收到后台确认，请重连后核对队列；未自动重发操作。"));
    else emit feedback(QStringLiteral("后台拒绝操作：") + response.value("reason").toString());
    refresh();
}

void PrintController::submit(const QString& title, int pages, int copies, int priority) {
    if (title.trimmed().isEmpty() || title.trimmed().size() > 200 || pages < 1 || pages > 1000 ||
        copies < 1 || copies > 100 || pages * copies > 10000 || priority < 0 || priority > 100) {
        emit feedback(QStringLiteral("提交失败：名称须为 1–200 字符，总页数不得超过 10000。")); return;
    }
    mutate("submit", {{"job", QJsonObject{{"job_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"title", title.trimmed()}, {"pages", pages}, {"copies", copies}, {"priority", priority}}}});
}

void PrintController::importFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        emit feedback(QStringLiteral("导入失败：文件无法读取或超过 4 MiB。")); return;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray() || doc.array().isEmpty() || doc.array().size() > 10000) {
        emit feedback(QStringLiteral("导入失败：需要 1–10000 条任务的 JSON 数组，未导入任何任务。")); return;
    }
    QJsonArray jobs;
    for (const auto& item : doc.array()) {
        auto job = item.toObject();
        const auto title = job.value("title").toString().trimmed();
        auto valid = [&](const char* key, int min, int max, int fallback) {
            if (!job.contains(key)) job.insert(key, fallback);
            const auto value = job.value(key); const double n = value.toDouble(-1);
            return value.isDouble() && n >= min && n <= max && std::floor(n) == n;
        };
        if (!item.isObject() || title.isEmpty() || title.size() > 200 || !valid("pages", 1, 1000, -1) ||
            !valid("copies", 1, 100, 1) || !valid("priority", 0, 100, 0) ||
            job.value("pages").toInt() * job.value("copies").toInt() > 10000) {
            emit feedback(QStringLiteral("导入失败：存在无效字段，未导入任何任务。")); return;
        }
        // Only send supported fields; backend validates the whole batch before writing.
        jobs.append(QJsonObject{{"job_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"title", title}, {"pages", job.value("pages")}, {"copies", job.value("copies")},
            {"priority", job.value("priority")}});
    }
    mutate("submit_batch", {{"jobs", jobs}});
}
void PrintController::cancel(const QString& id) { mutate("cancel", {{"job_id", id}}); }
void PrintController::setPolicy(int index) {
    if (index >= 0 && index <= 2) mutate("set_policy", {{"policy", QStringList{"fcfs", "sjf", "priority"}[index]}});
}
void PrintController::setPaused(bool paused) { mutate("set_paused", {{"paused", paused}}); }
void PrintController::checkConnection() { refresh(); }
void PrintController::injectFault(const QString& id) {
    if (options_.demoMode && demoAvailable_) mutate("crash_worker", {{"job_id", id}});
}
void PrintController::disconnectControl(const QString&) {
    if (options_.demoMode) { disconnected(); publish(); }
}

bool PrintController::applySnapshot(const QJsonObject& snapshot) {
    if (snapshot.value("type").toString() != "snapshot" || !snapshot.value("jobs").isArray() ||
        !snapshot.value("metrics").isObject() || snapshot.value("printers").toInt() != 2) return false;
    ViewState next;
    for (const auto& row : snapshot.value("jobs").toArray()) {
        Job job; QString error;
        if (!row.isObject() || !cps::jobSnapshotFromJson(row.toObject(), job, error)) return false;
        const auto object = row.toObject(); const auto id = QString::fromStdString(job.id);
        const auto title = QString::fromUtf8(job.title.data(), static_cast<qsizetype>(job.title.size()));
        const bool recovering = object.value("recovering").toBool();
        const int printed = object.value("printed_sheets").toInt();
        const auto detail = recovering ? QStringLiteral("后台正在核对旧执行结果") :
            QStringLiteral("后台快照 · 执行次数 %1").arg(job.attempts);
        next.jobs.append({id, title, job.sheets(), job.priority, job.state, printed, detail, true});
        const int slot = object.value("slot").toInt(-1);
        if (slot >= 0 && slot < 2) next.printers[slot] = {id, title,
            recovering ? QStringLiteral("恢复核对中") : job.state == JobState::Cancelling ? QStringLiteral("取消中") : QStringLiteral("打印中"),
            printed, job.sheets(), QStringLiteral("后台已连接")};
    }
    for (const auto& id : snapshot.value("waiting_order").toArray()) next.waitingOrder.append(id.toString());
    const auto policy = cps::policyFromName(snapshot.value("policy").toString());
    if (!policy) return false;
    next.policyIndex = *policy == Policy::Fcfs ? 0 : *policy == Policy::ShortestJobFirst ? 1 : 2;
    next.paused = snapshot.value("paused").toBool();
    const auto m = snapshot.value("metrics").toObject();
    auto& stats = next.metrics;
    stats.submitted = static_cast<std::size_t>(m.value("submitted").toDouble());
    stats.firstStarted = static_cast<std::size_t>(m.value("first_started").toDouble());
    stats.completed = static_cast<std::size_t>(m.value("completed").toDouble());
    stats.failed = static_cast<std::size_t>(m.value("failed").toDouble());
    stats.cancelled = static_cast<std::size_t>(m.value("cancelled").toDouble());
    stats.queued = static_cast<std::size_t>(m.value("queued").toDouble());
    stats.running = static_cast<std::size_t>(m.value("running").toDouble());
    stats.cancelling = static_cast<std::size_t>(m.value("cancelling").toDouble());
    stats.averageFirstWaitMs = m.value("average_first_wait_ms").toDouble();
    stats.averageTotalQueueWaitMs = m.value("average_total_queue_wait_ms").toDouble();
    stats.averageTurnaroundMs = m.value("average_turnaround_ms").toDouble();
    stats.busyPrinterMs = m.value("busy_printer_ms").toDouble();
    stats.throughputPerSecond = m.value("throughput_per_second").toDouble();
    stats.utilization = m.value("utilization").toDouble();
    stats.maxFirstWaitMs = static_cast<TimeMs>(m.value("max_first_wait_ms").toDouble());
    stats.maxCurrentQueueWaitMs = static_cast<TimeMs>(m.value("max_current_queue_wait_ms").toDouble());
    if (!available_) log(QStringLiteral("控制连接已连接 · 持久队列快照已同步"));
    available_ = true; demoAvailable_ = snapshot.value("demo_mode").toBool();
    snapshot_ = snapshot; view_ = next;
    return true;
}
void PrintController::refresh() {
    if (!applySnapshot(request("snapshot"))) disconnected();
    publish();
}
void PrintController::publish() {
    view_.connected = available_;
    view_.demoAvailable = demoAvailable_;
    view_.connection = available_ ? QStringLiteral("持久后台已连接 · 关闭窗口后继续执行") :
        QStringLiteral("后台暂不可用 · 正在重连，当前显示上次快照");
    for (auto& job : view_.jobs) job.controlReady = available_;
    emit viewChanged(view_);
    if (!logs_.isEmpty()) { emit logsReady(logs_); logs_.clear(); }
}
void PrintController::log(const QString& line) {
    if (logs_.size() >= 200) logs_.removeFirst();
    logs_.append(line.left(2000));
}
void PrintController::exportReport(const QString& path) {
    refresh();
    if (!available_) { emit feedback(QStringLiteral("报表未导出：无法获取后台最新快照。")); return; }
    auto report = snapshot_.value("metrics").toObject();
    report.insert("schema_version", 1); report.insert("mode", "persistent_simulation");
    report.insert("policy", snapshot_.value("policy"));
    report.insert("policy_note", "Policy may change during the persistent history; this is not a controlled policy comparison.");
    report.insert("window_start_ms", snapshot_.value("window_start_ms"));
    report.insert("window_end_ms", snapshot_.value("now_ms"));
    report.insert("printers", snapshot_.value("printers")); report.insert("page_ms", snapshot_.value("page_ms"));
    report.insert("jobs", snapshot_.value("jobs"));
    const auto data = QJsonDocument(report).toJson(); QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        emit feedback(QStringLiteral("报表写入失败：") + file.errorString());
    else emit feedback(QStringLiteral("报表已保存：") + path);
}
void PrintController::requestClose() {
    // Earlier queued commands have finished their IPC exchange before this slot runs.
    // Disconnecting this client never stops the persistent coordinator or its workers.
    if (refresh_) refresh_->stop();
    if (socket_) socket_->abort();
    available_ = false;
    emit closeReady(true);
}
} // namespace cps::gui
