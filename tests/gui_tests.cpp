#include "event_stream.hpp"
#include "print_window.hpp"
#include "paper_workspace.hpp"
#include "coordinator_test_client.hpp"
#include "windows_test_logs.hpp"
#include "cps/runtime/job_io.hpp"
#include <QProcess>
#include <memory>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
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
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
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
    void initTestCase() {
        const auto error = isolateTestReportHandles();
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    void shopWorkflow() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, true, true}); window.resize(1200, 800); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* fleet = window.findChild<QTableWidget*>("fleetTable");
        QTRY_COMPARE(fleet->rowCount(), 6);
        auto* revenue = window.findChild<QLabel*>("shopMetric0");
        auto* completed = window.findChild<QLabel*>("ordersCompleted");
        QTRY_COMPARE(revenue->text(), QStringLiteral("¥0.00"));
        QCOMPARE(completed->text(), QStringLiteral("0 单"));
        // Device actions follow the selected row instead of acting on nothing.
        auto* toggle = window.findChild<QPushButton*>("togglePrinter");
        auto* edit = window.findChild<QPushButton*>("editPrinter");
        QVERIFY(!toggle->isEnabled() && !edit->isEnabled());
        fleet->selectRow(0);
        QTRY_VERIFY(toggle->isEnabled() && edit->isEnabled());
        QCOMPARE(toggle->text(), QStringLiteral("暂停接单"));
        toggle->click();
        QTRY_COMPARE(fleet->item(0, 3)->text(), QStringLiteral("暂停接单"));
        QTRY_COMPARE(toggle->text(), QStringLiteral("恢复接单"));
        toggle->click();
        QTRY_COMPARE(fleet->item(0, 3)->text(), QStringLiteral("接单中"));
        openForm(window);
        window.findChild<QSpinBox*>("jobPages")->setValue(3);
        window.findChild<QSpinBox*>("jobCopies")->setValue(2);
        window.findChild<QComboBox*>("orderPaper")->setCurrentIndex(1);
        window.findChild<QCheckBox*>("orderColor")->setChecked(true);
        window.findChild<QCheckBox*>("orderDuplex")->setChecked(true);
        window.findChild<QComboBox*>("orderService")->setCurrentIndex(2);
        QTRY_VERIFY(window.findChild<QPushButton*>("submitJob")->isEnabled());
        window.findChild<QPushButton*>("submitJob")->click();
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("已完成"));
        // This window starts a demo backend: six A3 colour faces use the half-price tariff.
        QCOMPARE(table->model()->index(0, 8).data().toString(), QStringLiteral("¥7.27"));
        QTRY_COMPARE(revenue->text(), QStringLiteral("¥7.27"));
        QCOMPARE(window.findChild<QLabel*>("paid_counter")->text(), QStringLiteral("¥7.27"));
        QCOMPARE(completed->text(), QStringLiteral("1 单"));
        QCOMPARE(window.findChild<QLabel*>("ordersOnTime")->text(), QStringLiteral("1 单"));
        // Search narrows the records; clearing it brings the order back.
        auto* search = window.findChild<QLineEdit*>("queueSearch");
        search->setText(QStringLiteral("没有这样的订单"));
        QCOMPARE(table->model()->rowCount(), 0);
        search->setText(QStringLiteral("实验报告"));
        QCOMPARE(table->model()->rowCount(), 1);
        search->clear();
        auto* policies = window.findChild<QComboBox*>("schedulingPolicy");
        QCOMPARE(policies->count(), 3);
        policies->setCurrentIndex(2); QTRY_COMPARE(policies->currentIndex(), 2);
        window.experimentRequested(root.path() + "/experiments.json", 30);
        auto* experiment = window.findChild<QTableWidget*>("experimentTable");
        QTRY_COMPARE_WITH_TIMEOUT(experiment->rowCount(), 21, 15000);
        QCOMPARE(experiment->columnCount(), 11);
        QCOMPARE(experiment->horizontalHeaderItem(8)->text(), QStringLiteral("综合得分"));
        QVERIFY(experiment->item(0, 8)->text().toDouble() > 0);
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            for (const auto& name : QStringList{"navShop", "navDevices", "navHistory"}) {
                window.findChild<QPushButton*>(name)->click(); QTest::qWait(80);
                QVERIFY(window.grab().save(QDir(evidence).filePath("shop-" + name + ".png")));
            }
            openForm(window); QTest::qWait(80);
            QVERIFY(window.findChild<QDialog*>("submissionDialog")->grab().save(QDir(evidence).filePath("shop-order.png")));
            window.findChild<QDialog*>("submissionDialog")->reject();
        }
        // A refund needs a selected completed order and an explicit confirmation.
        auto* refund = window.findChild<QPushButton*>("refundOrder");
        window.findChild<QPushButton*>("navHistory")->click();
        QVERIFY(!refund->isEnabled());
        table->selectRow(0);
        QTRY_VERIFY(refund->isEnabled());
        refund->click();
        auto* confirmRefund = window.findChild<QDialog*>("refundDialog");
        QVERIFY(confirmRefund && confirmRefund->isVisible());
        QCOMPARE(revenue->text(), QStringLiteral("¥7.27")); // nothing happens before the confirmation
        if (!evidence.isEmpty()) QVERIFY(confirmRefund->grab().save(QDir(evidence).filePath("shop-refund.png")));
        confirmRefund->findChild<QPushButton*>("confirmAction")->click();
        QTRY_COMPARE(revenue->text(), QStringLiteral("¥0.00"));
        QCOMPARE(window.findChild<QLabel*>("ledgerRefund")->text(), QStringLiteral("¥7.27"));
        QTRY_VERIFY(!refund->isEnabled()); // already refunded in full
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

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
        QSignalSpy ready(&window, &PrintWindow::backendReady);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 15000);
        auto* title = window.findChild<QLineEdit*>("jobTitle");
        auto* pages = window.findChild<QSpinBox*>("jobPages");
        auto* submit = window.findChild<QPushButton*>("submitJob");
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* feedback = window.findChild<QLabel*>("feedback");
        QVERIFY(title && pages && submit && pause && table && feedback);
        QTest::mouseClick(pause, Qt::LeftButton);
        CoordinatorClient client(root.path());
        QJsonObject snapshot;
        // Wait for readiness and the asynchronous IPC pause before submitting
        // 20 ms jobs that must remain queued until cancellation.
        QTRY_VERIFY((snapshot = client.request("snapshot")).value("paused").toBool());
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
        QTRY_VERIFY(!(snapshot = client.request("snapshot")).isEmpty() && !snapshot.value("paused").toBool());
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
        QTRY_COMPARE(canvas->waitingIds().value(0), id(1));
        window.findChild<QPushButton*>("waitingJob0")->click();
        QCOMPARE(canvas->selectedId(), id(1));
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
            PrintWindow window({root.path(), 40, false});
            QSignalSpy ready(&window, &PrintWindow::backendReady);
            window.show();
            QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 15000);
            window.pauseRequested(true);
            CoordinatorClient client(root.path());
            QJsonObject paused;
            // Do not submit short jobs until the durable pause is visible.
            // The UI disables mutation before readiness; the test must too.
            QTRY_VERIFY((paused = client.request("snapshot")).value("paused").toBool());
            window.submitRequested(QStringLiteral("先来的大任务"), 40, 1, 0);
            window.submitRequested(QStringLiteral("短任务"), 2, 1, 0);
            window.submitRequested(QStringLiteral("紧急任务"), 10, 1, 100);
            auto* table = window.findChild<QTableView*>("jobsTable");
            QTRY_COMPARE(table->model()->rowCount(), 3);
            const auto first = client.request("snapshot");
            QVERIFY(first.value("paused").toBool());
            const auto rows = first.value("jobs").toArray();
            const auto id = [&](int i) { return rows[i].toObject().value("job_id").toString(); };
            for (int policy = 0; policy < 3; ++policy) {
                window.policyRequested(policy);
                auto* picker = window.findChild<QComboBox*>("schedulingPolicy");
                QTRY_COMPARE(picker->currentIndex(), policy);
                QJsonObject snap;
                const QStringList names{"fcfs", "sjf", "balanced"};
                QTRY_VERIFY((snap = client.request("snapshot")).value("policy").toString() == names[policy]);
                QCOMPARE(snap.value("waiting_order").toArray()[0].toString(), id(policy == 0 ? 0 : 1));
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
        bool verified = false;
        const auto diagnostics = qScopeGuard([&] {
            if (verified) return;
            // Only this test's generated state; retain evidence if recovery fails.
            for (const auto *name : {"state.json", "coordinator.log", ".coordinator.lock"}) {
                QFile file(QDir(root.path()).filePath(name));
                if (file.open(QIODevice::ReadOnly))
                    qInfo().noquote() << "Crash recovery" << name << QString::fromUtf8(file.read(16384));
            }
            qInfo().noquote() << "Crash recovery snapshot"
                << QJsonDocument(client.request("snapshot")).toJson(QJsonDocument::Compact);
            for (const auto& item : readObject(root.path() + "/state.json").value("jobs").toArray()) {
                const auto id = item.toObject().value("job_id").toString();
                for (const auto& name : {"." + id + ".supervisor.lock", "." + id + ".output.lock",
                                         id + ".events.jsonl"}) {
                    QFile file(QDir(root.path() + "/output").filePath(name));
                    qInfo().noquote() << "Crash recovery evidence" << name << "exists" << file.exists();
                    if (file.open(QIODevice::ReadOnly))
                        qInfo().noquote() << "bytes" << file.size() << QString::fromUtf8(file.read(8192));
                }
            }
            qInfo().noquote() << "Crash recovery control files"
                << QDir(root.path() + "/control").entryList(QDir::Files | QDir::Hidden);
            if (auto *log = window.findChild<QPlainTextEdit*>())
                qInfo().noquote() << "Crash recovery GUI log:" << log->toPlainText().right(8192);
        });
        // Crash immediately after the durable cancellation ACK, before its terminal event.
        QCOMPARE(client.request("cancel", {{"job_id", cancelledId}}).value("type").toString(), QString("accepted"));
        backend.kill(); QVERIFY(backend.waitForFinished(5000)); client.disconnect();
        // Reconnection and terminal reconciliation are separate asynchronous
        // phases. First prove that the GUI has launched a new coordinator;
        // neither a stale table row nor the old process's exit proves recovery.
        QJsonObject restarted;
        QTRY_VERIFY_WITH_TIMEOUT(!(restarted = client.request("snapshot")).isEmpty() &&
            restarted.value("coordinator_pid").toDouble() != originalPid, 20000);
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
        verified = true;
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
        auto* table = window.findChild<QTableView*>("jobsTable");
        // Native exposure does not imply a completed backend handshake.
        // Clicking the disabled submit button otherwise silently loses a job.
        QTRY_VERIFY_WITH_TIMEOUT(submit->isEnabled(), 15000);
        openForm(window);
        QTest::mouseClick(submit, Qt::LeftButton);
        QTRY_COMPARE(table->model()->rowCount(), 1);
        openForm(window);
        QTest::mouseClick(submit, Qt::LeftButton);
        window.findChild<QPushButton*>("navHistory")->click();
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

    void ownerConsoleShowsForecast() {
        BackendDirectory root;
        PrintWindow window({root.path(), 40, false});
        QSignalSpy backendReady(&window, &PrintWindow::backendReady);
        window.show();
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        QVERIFY(pause);
        // Receive a connected snapshot first; the widget's initial enabled
        // property by itself does not prove the coordinator is ready.
        QTRY_VERIFY_WITH_TIMEOUT(!backendReady.isEmpty(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(pause->isEnabled(), 10000);
        window.pauseRequested(true);
        QTRY_VERIFY_WITH_TIMEOUT(pause->isChecked(), 10000);
        for (int i = 0; i < 3; ++i) window.submitRequested(QStringLiteral("预计 %1").arg(i), 100, 1, 0);
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 3);
        QTRY_VERIFY(table->model()->index(2, 5).data().toString().contains(QStringLiteral("派发已暂停")));
        const auto pausedText = table->model()->index(2, 5).data().toString();
        for (int i = 0; i < 6; ++i) {
            QTest::qWait(250);
            QCOMPARE(table->model()->index(2, 5).data().toString(), pausedText);
        }
        CoordinatorClient client(root.path());
        const auto rows = client.request("snapshot").value("jobs").toArray();
        QCOMPARE(rows.size(), 3);
        // Two printers at 40 ms per page: the third order starts when the first one ends.
        const auto first = rows[0].toObject(), third = rows[2].toObject();
        QCOMPARE(third.value("jobs_ahead").toInt(), 2);
        QCOMPARE(third.value("estimated_start_ms").toDouble() - first.value("estimated_start_ms").toDouble(), 4000.0);
        QCOMPARE(third.value("estimated_finish_ms").toDouble() - third.value("estimated_start_ms").toDouble(), 4000.0);
        window.pauseRequested(false);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        QTRY_VERIFY(table->model()->index(0, 5).data().toString().startsWith(QStringLiteral("预计")));
        QTRY_VERIFY(table->model()->index(2, 5).data().toString().contains(QStringLiteral("前面 0 单")));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            window.resize(1280, 820); QTest::qWait(500);
            QVERIFY(window.grab().save(QDir(evidence).filePath("owner-forecast-workspace.png")));
            window.findChild<QPushButton*>("navHistory")->click(); QTest::qWait(80);
            QVERIFY(window.grab().save(QDir(evidence).filePath("owner-forecast-records.png")));
        }
        window.pauseRequested(true); // keep teardown from dispatching the waiting order
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void demoControlsAbsentInNormalMode() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        QSignalSpy ready(&window, &PrintWindow::backendReady);
        // Even this structural check starts a persistent coordinator. Await
        // its handshake so directory cleanup cannot race a detached launch.
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 15000);
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
