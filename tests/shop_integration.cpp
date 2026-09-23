#include "coordinator_test_client.hpp"
#include "cps/runtime/job_io.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void accepted(const QJsonObject& response) {
    if (response.value("type").toString() != "accepted") throw std::runtime_error(QJsonDocument(response).toJson().toStdString());
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        BackendDirectory root(QDir::tempPath() + QStringLiteral("/打印 店经营-XXXXXX"));
        require(root.isValid(), "temporary directory");
        QProcess process; process.setProgram(cps::siblingExecutable("print-coordinator"));
        process.setArguments({"--state-dir", root.path(), "--shop-mode", "--page-ms", "10", "--policy", "revenue"});
        process.start(); require(process.waitForStarted(5000), "start coordinator");
        CoordinatorClient client(root.path());
        QJsonObject snap;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 10000) {
            snap = client.request("snapshot"); if (!snap.isEmpty()) break; QThread::msleep(50);
        }
        require(snap.value("printers").toInt() == 6 && snap.value("fleet").toArray().size() == 6, "six heterogeneous devices");
        accepted(client.request("set_paused", {{"paused", true}}));
        QJsonObject order{{"job_id", "color-a3"}, {"title", QStringLiteral("彩色 双面海报")}, {"pages", 3}, {"copies", 2},
            {"paper", "a3"}, {"color", true}, {"duplex", true}, {"service", "flexible"}, {"quoted_cents", 1}};
        const auto quote = client.request("quote", {{"job", order}});
        require(quote.value("quoted_cents").toInt() == 1530 && quote.value("paper_sheets").toInt() == 4, "server quote");
        accepted(client.request("submit", {{"job", order}}));
        auto bad = order; bad.insert("job_id", "invalid"); bad.insert("paper", "a2");
        auto valid = order; valid.insert("job_id", "must-not-submit");
        require(client.request("submit_batch", {{"jobs", QJsonArray{valid, bad}}}).value("type").toString() == "error", "atomic invalid batch");
        accepted(client.request("set_printer", {{"index", 5}, {"online", false}}));
        accepted(client.request("set_paused", {{"paused", false}}));
        snap = client.request("snapshot");
        require(snap.value("jobs").toArray().size() == 1 && snap.value("jobs").toArray()[0].toObject().value("state") == "queued", "offline A3 device blocks compatible order");
        accepted(client.request("set_printer", {{"index", 5}, {"online", true}}));
        timer.restart();
        while (timer.elapsed() < 10000) {
            snap = client.request("snapshot");
            if (snap.value("shop_metrics").toObject().value("completed").toInt() == 1) break;
            QThread::msleep(30);
        }
        const auto job = snap.value("jobs").toArray()[0].toObject();
        require(job.value("state") == "completed" && job.value("assigned_printer").toInt() == 5, "compatible device executes");
        require(job.value("quoted_cents").toInt() == 1530, "client cannot override price");
        require(snap.value("stock_a3").toInt() == 2996, "duplex paper consumption");
        require(snap.value("shop_metrics").toObject().value("cost_cents").toInt() == 564, "paper and toner cost");
        QFile output(QDir(root.path()).filePath("output/color-a3.txt")); require(output.open(QIODevice::ReadOnly), "output exists");
        const auto text = output.readAll(); require(text.contains("paper=a3") && text.contains("physical_sheets=4"), "output specification");
        accepted(client.request("refund", {{"job_id", "color-a3"}, {"cumulative_cents", 1530}}));
        accepted(client.request("refund", {{"job_id", "color-a3"}, {"cumulative_cents", 1530}}));
        accepted(client.request("restock", {{"paper", "a3"}, {"quantity", 100}}));
        snap = client.request("snapshot");
        require(snap.value("stock_a3").toInt() == 3096 && snap.value("shop_metrics").toObject().value("revenue_cents").toInt() == 0, "restock and idempotent refund");
        accepted(client.request("shutdown")); require(process.waitForFinished(5000), "clean shutdown"); client.disconnect();
        cps::PersistentState state; QString error;
        require(cps::readPersistentState(QDir(root.path()).filePath("state.json"), state, error), "read shop snapshot");
        require(state.fleet.size() == 6 && state.jobs[0].refundCents == 1530, "commercial persistence");
        QFile original(QDir(root.path()).filePath("state.json")); require(original.open(QIODevice::ReadOnly), "read saved JSON");
        auto damaged = QJsonDocument::fromJson(original.readAll()).object(); auto rows = damaged.value("jobs").toArray();
        auto missingPrice = rows[0].toObject(); missingPrice.remove("quoted_cents"); rows[0] = missingPrice; damaged.insert("jobs", rows);
        QFile badFile(QDir(root.path()).filePath("bad-state.json")); require(badFile.open(QIODevice::WriteOnly), "write invalid snapshot");
        badFile.write(QJsonDocument(damaged).toJson()); badFile.close();
        cps::PersistentState rejected;
        require(!cps::readPersistentState(badFile.fileName(), rejected, error), "partial commercial data must not silently reset pricing");
        process.start(); require(process.waitForStarted(5000), "reopen shop");
        timer.restart(); snap = {};
        while (timer.elapsed() < 10000) { snap = client.request("snapshot"); if (!snap.isEmpty()) break; QThread::msleep(50); }
        require(snap.value("stock_a3").toInt() == 3096 && snap.value("jobs").toArray().size() == 1, "stock and orders restored");
        accepted(client.request("shutdown")); require(process.waitForFinished(5000), "stop reopened shop");
        std::cout << "Shop quote, order, device, stock, settlement and persistence workflow passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
