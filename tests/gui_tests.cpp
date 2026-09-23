#include "event_stream.hpp"
#include "print_window.hpp"
#include "paper_workspace.hpp"
#include "coordinator_test_client.hpp"
#include "cps/runtime/job_io.hpp"
#include <QProcess>
#include <memory>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableView>
#include <QToolButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTest>
#include <QElapsedTimer>
#include <QDialog>
#include <QListWidget>

using namespace cps::gui;

class GuiTests : public QObject {
    Q_OBJECT
    static void openForm(PrintWindow& window) {
        window.findChild<QPushButton*>("newTask")->click();
        auto* dialog = window.findChild<QDialog*>("submissionDialog");
        QVERIFY(dialog && dialog->isVisible());
    }
private slots:
    void framing() {
        const QByteArray line = QJsonDocument(QJsonObject{{"schema_version", 1},
            {"type", "failed"}, {"job_id", "job-1"}, {"reason", QStringLiteral("中文错误")}})
            .toJson(QJsonDocument::Compact) + '\n';
        // Every byte boundary includes splitting inside a UTF-8 character.
        for (qsizetype split = 0; split <= line.size(); ++split) {
            EventStream stream("job-1");
            auto first = stream.append(line.left(split));
            auto second = stream.append(line.mid(split));
            QCOMPARE(first.events.size() + second.events.size(), 1);
            QVERIFY(first.errors.isEmpty());
            QVERIFY(second.errors.isEmpty());
            QVERIFY(stream.finish().errors.isEmpty());
        }
        EventStream stream("job-1");
        QCOMPARE(stream.append(line + line + line.left(8)).events.size(), 2);
        QCOMPARE(stream.append(line.mid(8)).events.size(), 1);
        QCOMPARE(stream.append(line.trimmed() + "\r\n\n").events.size(), 1);
    }

    void invalidEventsAndRecovery() {
        EventStream stream("job-1");
        const QByteArray good = "{\"schema_version\":1,\"type\":\"progress\",\"job_id\":\"job-1\","
            "\"printed_sheets\":1,\"total_sheets\":8}\n";
        for (const auto& bad : {QByteArray("garbage\n"), QByteArray("[]\n"),
             QByteArray(good).replace("job-1", "job-2"),
             QByteArray(good).replace("\"schema_version\":1", "\"schema_version\":2"),
             QByteArray(good).replace("\"printed_sheets\":1", "\"printed_sheets\":1.5"),
             QByteArray(good).replace("\"printed_sheets\":1", "\"printed_sheets\":9")}) {
            auto result = stream.append(bad + good);
            QCOMPARE(result.events.size(), 1);
            QCOMPARE(result.errors.size(), 1);
        }
        QVERIFY(!stream.append(QByteArray(EventStream::MaxFrameBytes + 1, 'x')).errors.isEmpty());
        QCOMPARE(stream.append("more discarded bytes\n" + good).events.size(), 1);
        stream.append(good.left(10));
        QCOMPARE(stream.finish().errors.size(), 1);
        QVERIFY(stream.finish().errors.isEmpty());
    }

    void interactiveQueueAndCompletion() {
        BackendDirectory root(QDir::tempPath() + QStringLiteral("/界面 测试-XXXXXX"));
        QVERIFY(root.isValid());
        PrintWindow window({root.path(), 10, false});
        window.show();
        auto* title = window.findChild<QLineEdit*>("jobTitle");
        auto* pages = window.findChild<QSpinBox*>("jobPages");
        auto* submit = window.findChild<QPushButton*>("submitJob");
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* feedback = window.findChild<QLabel*>("feedback");
        QVERIFY(title && pages && submit && pause && table && feedback);
        QTest::mouseClick(pause, Qt::LeftButton);
        openForm(window);
        title->setText("   ");
        QTest::mouseClick(submit, Qt::LeftButton);
        QTRY_VERIFY(feedback->text().contains(QStringLiteral("名称")));
        QCOMPARE(table->model()->rowCount(), 0);
        title->setText(QStringLiteral("中文 交互验收"));
        pages->setValue(2);
        for (int i = 0; i < 3; ++i) { openForm(window); QTest::mouseClick(submit, Qt::LeftButton); }
        QTRY_COMPARE(table->model()->rowCount(), 3);
        window.findChild<QPushButton*>("navHistory")->click();
        table->selectRow(0);
        auto* cancel = window.findChild<QPushButton*>("cancelJob");
        QTRY_VERIFY(cancel->isEnabled());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("已取消"));
        window.findChild<QPushButton*>("navWorkspace")->click();
        QTest::mouseClick(pause, Qt::LeftButton);
        window.findChild<QPushButton*>("navHistory")->click();
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(1, 3).data().toString(), QStringLiteral("已完成"), 10000);
        QTRY_COMPARE(table->model()->index(2, 3).data().toString(), QStringLiteral("已完成"));
        QCOMPARE(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).size(), 2);
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }

    void commandPaletteControlsDispatch() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false}); window.show();
        auto* command = window.findChild<QPushButton*>("commandPalette");
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        QVERIFY(command && pause);
        bool foundDialog = false;
        QTimer::singleShot(50, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("commandDialog");
            if (!dialog) return;
            auto* query = dialog->findChild<QLineEdit*>("commandQuery");
            auto* choices = dialog->findChild<QListWidget*>("commandChoices");
            if (!query || !choices) { dialog->reject(); return; }
            foundDialog = true; query->setText(QStringLiteral("暂停"));
            QTest::keyClick(query, Qt::Key_Return);
        });
        // A guard prevents a broken modal interaction from hanging the test suite.
        QTimer::singleShot(2000, &window, [&] { auto* dialog=window.findChild<QDialog*>("commandDialog"); if(dialog)dialog->reject(); });
        command->click();
        QVERIFY(foundDialog); QVERIFY(pause->isChecked());
        auto* motion = window.findChild<QPushButton*>("reduceMotion");
        QVERIFY(motion); motion->click();
        QVERIFY(motion->isChecked());
        // These visual controls must not resume dispatch or create jobs.
        QVERIFY(pause->isChecked()); QCOMPARE(window.findChild<QTableView*>("jobsTable")->model()->rowCount(),0);
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void largeQueueRemainsInteractive() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        window.show();
        QTest::mouseClick(window.findChild<QPushButton*>("pauseDispatch"), Qt::LeftButton);
        QJsonArray jobs;
        for (int i = 0; i < 5000; ++i) {
            jobs.append(QJsonObject{{"title", QStringLiteral("批量 %1").arg(i)}, {"pages", 1}, {"copies", 1}});
        }
        QFile workload(root.path() + "/workload.json");
        QVERIFY(workload.open(QIODevice::WriteOnly));
        workload.write(QJsonDocument(jobs).toJson(QJsonDocument::Compact));
        workload.close();
        int ticks = 0;
        qint64 longestGap = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            ++ticks;
            longestGap = std::max(longestGap, elapsed.restart());
        });
        timer.start(5);
        window.importFile(workload.fileName());
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 5000, 15000);
        QVERIFY(ticks > 0);
        auto* title = window.findChild<QLineEdit*>("jobTitle");
        openForm(window);
        title->clear();
        QTest::keyClicks(title, "still responsive");
        QCOMPARE(title->text(), QString("still responsive"));
        QVERIFY2(longestGap < 2000, "GUI event loop stalled for two seconds during import");
        qInfo("5000-job import: heartbeat ticks=%d, longest observed gap=%lld ms", ticks, static_cast<long long>(longestGap));
        // No executor was started while the local queue was paused.
        QVERIFY(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).isEmpty());
        window.findChild<QDialog*>("submissionDialog")->reject();
    }

    void paperWorkbenchTracksPolicyAndRealJobs() {
        BackendDirectory root;
        PrintWindow window({root.path(), 100, false});
        window.resize(1380, 860); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        pause->click();
        window.submitRequested(QStringLiteral("操作系统实验报告"), 30, 1, 1);
        window.submitRequested(QStringLiteral("实验数据"), 2, 1, 0);
        window.submitRequested(QStringLiteral("阅读材料"), 10, 1, 9);
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* canvas = dynamic_cast<PaperWorkspace*>(window.findChild<QWidget*>("paperWorkspace"));
        auto* policy = window.findChild<QComboBox*>("schedulingPolicy");
        QVERIFY(canvas && policy);
        QTRY_COMPARE(table->model()->rowCount(), 3);
        const auto id = [&](int row) { return table->model()->index(row, 0).data(Qt::UserRole).toString(); };
        QTRY_COMPARE(canvas->waitingIds().value(0), id(0));
        policy->setCurrentIndex(1);
        QTRY_COMPARE(canvas->waitingIds().value(0), id(1));
        policy->setCurrentIndex(2);
        QTRY_COMPARE(canvas->waitingIds().value(0), id(2));
        window.findChild<QPushButton*>("waitingJob0")->click();
        QCOMPARE(canvas->selectedId(), id(2));
        policy->setCurrentIndex(0);
        QTRY_COMPARE(canvas->waitingIds().value(0), id(0));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        auto screenshot = [&](const QString& name) {
            return evidence.isEmpty() || (QDir().mkpath(evidence) && window.grab().save(QDir(evidence).filePath(name)));
        };
        QTest::qWait(450); QVERIFY(screenshot("paper-01-queued.png"));
        pause->click();
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        policy->setCurrentIndex(2);
        // Policy changes must not preempt the already running long task.
        QCOMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        window.findChild<QPushButton*>("waitingJob0")->click();
        QTest::qWait(180);
        QVERIFY(screenshot("paper-02-running.png"));
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0, 3).data().toString(), QStringLiteral("已完成"), 10000);
        QTRY_COMPARE(table->model()->index(2, 3).data().toString(), QStringLiteral("已完成"));
        QTRY_VERIFY(canvas->waitingIds().isEmpty());
        QCOMPARE(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).size(), 3);
        QVERIFY(screenshot("paper-03-completed.png"));
        openForm(window);
        if (!evidence.isEmpty()) QVERIFY(window.findChild<QDialog*>("submissionDialog")->grab().save(QDir(evidence).filePath("paper-04-form.png")));
        window.findChild<QDialog*>("submissionDialog")->reject();
        window.resize(1000, 680);
        auto* scroll = window.findChild<QScrollArea*>("workbenchScroll");
        scroll->ensureWidgetVisible(pause);
        QTRY_VERIFY(scroll->viewport()->rect().contains(QRect(pause->mapTo(scroll->viewport(), QPoint(0, 0)), pause->size())));
        QVERIFY(screenshot("paper-05-compact.png"));
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void invalidBatchIsAtomicAndReportIsWritten() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        window.show();
        QTest::mouseClick(window.findChild<QPushButton*>("pauseDispatch"), Qt::LeftButton);
        QFile workload(root.path() + "/invalid.json");
        QVERIFY(workload.open(QIODevice::WriteOnly));
        workload.write("[{\"title\":\"valid\",\"pages\":1},{\"title\":\"bad\",\"pages\":1.5}]");
        workload.close();
        window.importFile(workload.fileName());
        QTRY_VERIFY(window.findChild<QLabel*>("feedback")->text().contains(QStringLiteral("未导入任何任务")));
        QCOMPARE(window.findChild<QTableView*>("jobsTable")->model()->rowCount(), 0);
        window.exportRequested(root.path() + "/report.json");
        QTRY_VERIFY(QFile::exists(root.path() + "/report.json"));
        QFile report(root.path() + "/report.json");
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(report.readAll()).object();
        QCOMPARE(json.value("mode").toString(), QString("persistent_simulation"));
        QCOMPARE(json.value("completed").toInt(), 0);
    }

    void closeCannotRaceSubmission() {
        BackendDirectory root;
        QString id;
        {
            PrintWindow window({root.path(), 30, false}); window.show();
            window.submitRequested(QStringLiteral("关闭前提交"), 100, 1, 0);
            window.close(); // Serialized after submit, before its first visual snapshot.
            QTRY_VERIFY(!window.isVisible());
            CoordinatorClient client(root.path());
            const auto jobs = client.request("snapshot").value("jobs").toArray();
            QCOMPARE(jobs.size(), 1); id = jobs[0].toObject().value("job_id").toString();
        }
        PrintWindow reopened({root.path(), 30, false}); reopened.show();
        auto* table = reopened.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 0).data(Qt::UserRole).toString(), id);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0, 3).data().toString(), QStringLiteral("已完成"), 10000);
        reopened.close(); QTRY_VERIFY(!reopened.isVisible());
    }

    static QJsonObject readObject(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    void persistentPoliciesAndReport() {
        BackendDirectory root(QDir::tempPath() + QStringLiteral("/持久 验收-XXXXXX"));
        {
            PrintWindow window({root.path(), 40, false}); window.show();
            window.pauseRequested(true);
            window.submitRequested(QStringLiteral("先来的大任务"), 40, 1, 0);
            window.submitRequested(QStringLiteral("短任务"), 2, 1, 0);
            window.submitRequested(QStringLiteral("紧急任务"), 10, 1, 100);
            auto* table = window.findChild<QTableView*>("jobsTable");
            QTRY_COMPARE(table->model()->rowCount(), 3);
            CoordinatorClient client(root.path());
            const auto first = client.request("snapshot");
            QVERIFY(first.value("paused").toBool());
            const auto rows = first.value("jobs").toArray();
            const auto id = [&](int i) { return rows[i].toObject().value("job_id").toString(); };
            for (int policy = 0; policy < 3; ++policy) {
                window.policyRequested(policy);
                auto* picker = window.findChild<QComboBox*>("schedulingPolicy");
                QTRY_COMPARE(picker->currentIndex(), policy);
                QJsonObject snap;
                const QStringList names{"fcfs", "sjf", "priority"};
                QTRY_VERIFY((snap = client.request("snapshot")).value("policy").toString() == names[policy]);
                QCOMPARE(snap.value("waiting_order").toArray()[0].toString(), id(policy));
                for (const auto& j : snap.value("jobs").toArray()) QCOMPARE(j.toObject().value("attempts").toInt(), 0);
            }
            // Backend validation is atomic too, independent of the GUI's input checks.
            const QJsonObject valid{{"job_id", "atomic-new"}, {"title", "valid"}, {"pages", 1}, {"copies", 1}};
            auto duplicate = valid; duplicate.insert("job_id", id(0));
            QCOMPARE(client.request("submit_batch", {{"jobs", QJsonArray{valid, duplicate}}}).value("type").toString(), QString("error"));
            QCOMPARE(client.request("snapshot").value("jobs").toArray().size(), 3);
            window.close(); QTRY_VERIFY(!window.isVisible());
        }
        PrintWindow reopened({root.path(), 40, false}); reopened.show();
        auto* table = reopened.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 3);
        QTRY_VERIFY(reopened.findChild<QPushButton*>("pauseDispatch")->isChecked());
        QTRY_COMPARE(reopened.findChild<QComboBox*>("schedulingPolicy")->currentIndex(), 2);
        reopened.pauseRequested(false);
        for (int i = 0; i < 3; ++i)
            QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(i, 3).data().toString(), QStringLiteral("已完成"), 15000);
        const auto path = root.path() + "/audit.json";
        reopened.exportRequested(path);
        QTRY_VERIFY(QFile::exists(path));
        const auto report = readObject(path);
        QCOMPARE(report.value("mode").toString(), QString("persistent_simulation"));
        QCOMPARE(report.value("completed").toInt(), 3);
        QCOMPARE(report.value("cancelled").toInt(), 0);
        const double start = report.value("window_start_ms").toDouble();
        const double end = report.value("window_end_ms").toDouble();
        double waits = 0, turns = 0, busy = 0, maxWait = 0;
        for (const auto& item : report.value("jobs").toArray()) {
            const auto j = item.toObject();
            const double submitted = j.value("submitted_ms").toDouble();
            const double started = j.value("first_started_ms").toDouble();
            const double finished = j.value("finished_ms").toDouble();
            const double wait = started - submitted;
            QVERIFY(started >= submitted && finished >= started && finished <= end);
            waits += wait; turns += finished - submitted; maxWait = std::max(maxWait, wait);
            QCOMPARE(j.value("accumulated_wait_ms").toDouble(), wait);
            QCOMPARE(j.value("attempts").toInt(), 1);
            const auto runs = j.value("runs").toArray(); QCOMPARE(runs.size(), 1);
            busy += runs[0].toObject().value("stopped_ms").toDouble() - runs[0].toObject().value("started_ms").toDouble();
            QFile output(root.path() + "/output/" + j.value("job_id").toString() + ".txt");
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll().count("sheet="), j.value("pages").toInt() * j.value("copies").toInt());
            QFile events(root.path() + "/output/" + j.value("job_id").toString() + ".events.jsonl");
            QVERIFY(events.open(QIODevice::ReadOnly));
            QCOMPARE(events.readAll().count("worker_started"), 1);
        }
        const auto near = [&](const char* field, double expected) {
            return std::abs(report.value(field).toDouble() - expected) < 1e-6 * std::max(1.0, std::abs(expected));
        };
        QVERIFY(near("average_first_wait_ms", waits / 3));
        QVERIFY(near("average_total_queue_wait_ms", waits / 3));
        QVERIFY(near("average_turnaround_ms", turns / 3));
        QVERIFY(near("max_first_wait_ms", maxWait));
        QVERIFY(near("busy_printer_ms", busy));
        QVERIFY(near("throughput_per_second", 3000.0 / (end - start)));
        QVERIFY(near("utilization", busy / (2 * (end - start))));
        reopened.close(); QTRY_VERIFY(!reopened.isVisible());
        PrintWindow again({root.path(), 40, false}); again.show();
        again.exportRequested(root.path() + "/after-reopen.json");
        QTRY_VERIFY(QFile::exists(root.path() + "/after-reopen.json"));
        const auto restored = readObject(root.path() + "/after-reopen.json");
        QCOMPARE(restored.value("completed").toInt(), 3);
        QCOMPARE(restored.value("window_start_ms"), report.value("window_start_ms"));
        QCOMPARE(restored.value("average_first_wait_ms"), report.value("average_first_wait_ms"));
        again.close(); QTRY_VERIFY(!again.isVisible());
    }

    void backendCrashDuringCancellation() {
        BackendDirectory root;
        QProcess backend;
        backend.setProgram(cps::siblingExecutable("print-coordinator"));
        backend.setArguments({"--state-dir", root.path(), "--page-ms", "20"});
        backend.start(); QVERIFY(backend.waitForStarted(5000));
        CoordinatorClient client(root.path());
        QJsonObject initial;
        QTRY_VERIFY(!(initial = client.request("snapshot")).isEmpty());
        const double originalPid = initial.value("coordinator_pid").toDouble();
        PrintWindow window({root.path(), 20, false}); window.show();
        window.submitRequested("cancel across crash", 200, 1, 0);
        window.submitRequested("recover remaining work", 100, 1, 0);
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 2);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        const auto cancelledId = table->model()->index(0, 0).data(Qt::UserRole).toString();
        // Crash immediately after the durable cancellation ACK, before its terminal event.
        QCOMPARE(client.request("cancel", {{"job_id", cancelledId}}).value("type").toString(), QString("accepted"));
        backend.kill(); QVERIFY(backend.waitForFinished(5000)); client.disconnect();
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0, 3).data().toString(), QStringLiteral("已取消"), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(1, 3).data().toString(), QStringLiteral("已完成"), 20000);
        const auto recovered = client.request("snapshot");
        QVERIFY(recovered.value("coordinator_pid").toDouble() != originalPid);
        QCOMPARE(recovered.value("metrics").toObject().value("cancelled").toInt(), 1);
        QCOMPARE(recovered.value("metrics").toObject().value("completed").toInt(), 1);
        for (const auto& item : recovered.value("jobs").toArray()) {
            const auto job = item.toObject();
            if (job.value("job_id").toString() == cancelledId) {
                QCOMPARE(job.value("attempts").toInt(), 1);
                QVERIFY(!QFile::exists(root.path() + "/output/" + cancelledId + ".txt"));
            } else {
                QFile file(root.path() + "/output/" + job.value("job_id").toString() + ".txt");
                QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll().count("sheet="), 100);
            }
        }
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void runningCancelFaultAndReconnect() {
        BackendDirectory root;
        PrintWindow window({root.path(), 30, true});
        window.show();
        // Cocoa activation/exposure is asynchronous. Don't start short-lived
        // work before the native window is available for interaction.
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* pages = window.findChild<QSpinBox*>("jobPages");
        pages->setValue(1000); // Keep both jobs alive until explicitly cancelled.
        auto* submit = window.findChild<QPushButton*>("submitJob");
        openForm(window);
        QTest::mouseClick(submit, Qt::LeftButton);
        openForm(window);
        QTest::mouseClick(submit, Qt::LeftButton);
        window.findChild<QPushButton*>("navHistory")->click();
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 2);
        table->selectRow(0);
        auto* fault = window.findChild<QPushButton*>("injectFault");
        auto* disconnect = window.findChild<QPushButton*>("disconnectControl");
        auto* cancel = window.findChild<QPushButton*>("cancelJob");
        auto* log = window.findChild<QPlainTextEdit*>();
        QVERIFY(fault && disconnect && cancel && log);
        QTRY_VERIFY2(fault->isEnabled(), qPrintable(QStringLiteral("state=%1 ready=%2 current=%3; %4")
            .arg(table->model()->index(0, 3).data().toString())
            .arg(table->model()->index(0, 0).data(Qt::UserRole + 2).toBool())
            .arg(table->currentIndex().row()).arg(log->toPlainText())));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        auto screenshot = [&](const QString& name) {
            if (evidence.isEmpty()) return true;
            return QDir().mkpath(evidence) && window.grab().save(QDir(evidence).filePath(name));
        };
        QVERIFY(screenshot("01-connected.png"));
        QTest::mouseClick(fault, Qt::LeftButton);
        QTRY_VERIFY(log->toPlainText().contains("worker_restarting"));
        QTest::mouseClick(disconnect, Qt::LeftButton);
        QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("控制连接断开")));
        QTRY_VERIFY(cancel->isEnabled());
        QVERIFY(screenshot("02-reconnected.png"));
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("已取消"));
        table->selectRow(1);
        QTRY_VERIFY(cancel->isEnabled());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE(table->model()->index(1, 3).data().toString(), QStringLiteral("已取消"));
        QVERIFY(screenshot("03-cancelled.png"));
        QVERIFY(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).isEmpty());
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }

    void demoControlsAbsentInNormalMode() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        QVERIFY(!window.findChild<QPushButton*>("injectFault"));
        QVERIFY(!window.findChild<QPushButton*>("disconnectControl"));
    }

    void compactWindowKeepsControlsReachable() {
        BackendDirectory root;
        PrintWindow window({root.path(), 30, true});
        window.resize(1060, 720);
        window.show();
        window.findChild<QPushButton*>("navHistory")->click();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* fault = window.findChild<QPushButton*>("injectFault");
        auto* area = window.findChild<QScrollArea*>("queueScrollArea");
        auto* toggle = window.findChild<QToolButton*>("logToggle");
        auto* log = window.findChild<QPlainTextEdit*>();
        QVERIFY(table && fault && area && toggle && log);
        QVERIFY(!log->isVisible());
        for (bool showLog : {false, true}) {
            if (showLog) QTest::mouseClick(toggle, Qt::LeftButton);
            QCOMPARE(log->isVisible(), showLog);
            // Regression: at 720 px, the old fixed layout drew buttons over task rows.
            QTRY_VERIFY(table->mapTo(&window, QPoint(0, table->height())).y()
                        <= fault->mapTo(&window, QPoint(0, 0)).y());
            area->ensureWidgetVisible(fault, 0, 0);
            QTRY_VERIFY(area->viewport()->rect().contains(
                QRect(fault->mapTo(area->viewport(), QPoint(0, 0)), fault->size())));
        }
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(window.grab().save(QDir(evidence).filePath("04-compact-with-logs.png")));
        }
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }
};

QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
