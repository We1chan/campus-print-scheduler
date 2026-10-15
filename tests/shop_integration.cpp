#include "coordinator_test_client.hpp"
#include "cps/runtime/job_io.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QFile>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void accepted(const QJsonObject& response, const char* context = "command") {
    if (response.value("type").toString() != "accepted")
        throw std::runtime_error(std::string(context) + ": " + QJsonDocument(response).toJson().toStdString());
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
        require(snap.value("policy") == "balanced", "retired startup name migrates to balanced");
        accepted(client.request("set_paused", {{"paused", true}}), "pause new shop");
        QJsonObject order{{"job_id", "color-a3"}, {"title", QStringLiteral("彩色 双面海报")}, {"pages", 3}, {"copies", 2},
            {"paper", "a3"}, {"color", true}, {"duplex", true}, {"service", "flexible"}, {"quoted_cents", 1},
            {"payment", "wechat"}, {"customer", QStringLiteral("2024000001")}};
        const auto quote = client.request("quote", {{"job", order}});
        require(quote.value("quoted_cents").toInt() == 1454 && quote.value("paper_sheets").toInt() == 4, "server quote");
        // 6 colour A3 sides on the 40 ms device: 6 * 40 * 2. Paused dispatch is forecast as if resumed now.
        require(quote.value("estimate_blocked").toString().isEmpty() && quote.value("jobs_ahead").toInt() == 0 &&
                quote.value("estimated_wait_ms").toDouble() == 0 && quote.value("paused").toBool() &&
                quote.value("estimated_finish_ms").toDouble() - quote.value("estimated_start_ms").toDouble() == 480, "quote forecasts an idle device");
        accepted(client.request("submit", {{"job", order}}), "submit color order");
        auto probe = order; probe.insert("job_id", "quote-only");
        const auto second = client.request("quote", {{"job", probe}});
        require(second.value("jobs_ahead").toInt() == 1 && second.value("estimated_wait_ms").toDouble() == 480, "quote waits behind the queued order");
        snap = client.request("snapshot");
        auto row = snap.value("jobs").toArray()[0].toObject();
        require(snap.value("jobs").toArray().size() == 1 && row.value("payment") == "wechat" && row.value("customer") == "2024000001" &&
                row.value("estimate_blocked").toString().isEmpty() && row.value("jobs_ahead").toInt() == 0 &&
                row.value("estimated_finish_ms").toDouble() - row.value("estimated_start_ms").toDouble() == 480 &&
                snap.value("queue_clear_ms").toDouble() == row.value("estimated_finish_ms").toDouble(), "snapshot carries the forecast and payment");
        require(snap.value("shop_metrics").toObject().value("prepaid_cents").toInt() == 1454, "online payment is held as prepayment");
        auto unpaid = order; unpaid.insert("job_id", "bad-payment"); unpaid.insert("payment", "bitcoin");
        require(client.request("submit", {{"job", unpaid}}).value("type").toString() == "error", "unknown payment method rejected");
        unpaid.insert("payment", "alipay"); unpaid.insert("customer", QString(65, 'x'));
        require(client.request("submit", {{"job", unpaid}}).value("type").toString() == "error", "oversized customer rejected");
        auto bad = order; bad.insert("job_id", "invalid"); bad.insert("paper", "a2");
        auto valid = order; valid.insert("job_id", "must-not-submit");
        require(client.request("submit_batch", {{"jobs", QJsonArray{valid, bad}}}).value("type").toString() == "error", "atomic invalid batch");
        accepted(client.request("set_printer", {{"index", 5}, {"online", false}}), "disable A3 printer");
        accepted(client.request("set_paused", {{"paused", false}}), "resume shop");
        snap = client.request("snapshot");
        require(snap.value("jobs").toArray().size() == 1 && snap.value("jobs").toArray()[0].toObject().value("state") == "queued", "offline A3 device blocks compatible order");
        row = snap.value("jobs").toArray()[0].toObject();
        require(row.value("estimate_blocked") == "no_device" && !row.contains("estimated_start_ms") &&
                snap.value("blocked_orders").toInt() == 1 && snap.value("queue_clear_ms").isNull(), "blocked order has no forecast");
        require(client.request("quote", {{"job", probe}}).value("estimate_blocked") == "no_device", "quote reports the blocked device");
        accepted(client.request("set_printer", {{"index", 5}, {"online", true}}), "enable A3 printer");
        timer.restart();
        while (timer.elapsed() < 10000) {
            snap = client.request("snapshot");
            if (snap.value("shop_metrics").toObject().value("completed").toInt() == 1) break;
            QThread::msleep(30);
        }
        const auto job = snap.value("jobs").toArray()[0].toObject();
        require(job.value("state") == "completed" && job.value("assigned_printer").toInt() == 5, "compatible device executes");
        require(job.value("quoted_cents").toInt() == 1454, "client cannot override price");
        require(snap.value("stock_a3").toInt() == 2996, "duplex paper consumption");
        require(snap.value("shop_metrics").toObject().value("cost_cents").toInt() == 564, "paper and toner cost");
        require(snap.value("shop_metrics").toObject().value("prepaid_cents").toInt() == 0 &&
                snap.value("shop_metrics").toObject().value("paid_cents").toObject().value("wechat").toInt() == 1454 &&
                !job.contains("estimate_blocked"), "completed order settles into its payment channel");
        QFile output(QDir(root.path()).filePath("output/color-a3.txt")); require(output.open(QIODevice::ReadOnly), "output exists");
        const auto text = output.readAll(); require(text.contains("paper=a3") && text.contains("physical_sheets=4"), "output specification");
        accepted(client.request("refund", {{"job_id", "color-a3"}, {"cumulative_cents", 1454}}), "refund order");
        accepted(client.request("refund", {{"job_id", "color-a3"}, {"cumulative_cents", 1454}}), "repeat refund");
        accepted(client.request("restock", {{"paper", "a3"}, {"quantity", 100}}), "restock A3");
        snap = client.request("snapshot");
        require(snap.value("stock_a3").toInt() == 3096 && snap.value("shop_metrics").toObject().value("revenue_cents").toInt() == 0, "restock and idempotent refund");
        accepted(client.request("shutdown"), "close original shop"); require(process.waitForFinished(5000), "clean shutdown"); client.disconnect();
        cps::PersistentState state; QString error;
        require(cps::readPersistentState(QDir(root.path()).filePath("state.json"), state, error), "read shop snapshot");
        require(state.fleet.size() == 6 && state.jobs[0].refundCents == 1454, "commercial persistence");
        require(state.jobs[0].payment == cps::PaymentMethod::Wechat && state.jobs[0].customer == "2024000001", "payment persistence");
        QFile original(QDir(root.path()).filePath("state.json")); require(original.open(QIODevice::ReadOnly), "read saved JSON");
        auto damaged = QJsonDocument::fromJson(original.readAll()).object(); auto rows = damaged.value("jobs").toArray();
        original.close(); // Windows cannot replace a state file held open by the test.
        require(damaged.value("schema_version").toInt() == 3, "payment fields use persistent schema 3");
        // A v0.2 (schema 2) file has no payment fields and must still load as a counter order.
        auto older = damaged; auto olderRow = rows[0].toObject(); olderRow.remove("payment"); olderRow.remove("customer");
        older.insert("schema_version", 2); older.insert("jobs", QJsonArray{olderRow});
        QFile olderFile(QDir(root.path()).filePath("schema2-state.json")); require(olderFile.open(QIODevice::WriteOnly), "write schema 2 snapshot");
        olderFile.write(QJsonDocument(older).toJson()); olderFile.close();
        cps::PersistentState upgraded;
        require(cps::readPersistentState(olderFile.fileName(), upgraded, error) && upgraded.jobs[0].customer.empty() &&
                upgraded.jobs[0].payment == cps::PaymentMethod::Counter, "schema 2 shop state remains readable");
        auto missingPrice = rows[0].toObject(); missingPrice.remove("quoted_cents"); rows[0] = missingPrice; damaged.insert("jobs", rows);
        QFile badFile(QDir(root.path()).filePath("bad-state.json")); require(badFile.open(QIODevice::WriteOnly), "write invalid snapshot");
        badFile.write(QJsonDocument(damaged).toJson()); badFile.close();
        cps::PersistentState rejected;
        require(!cps::readPersistentState(badFile.fileName(), rejected, error), "partial commercial data must not silently reset pricing");
        process.start(); require(process.waitForStarted(5000), "reopen shop");
        timer.restart(); snap = {};
        while (timer.elapsed() < 10000) { snap = client.request("snapshot"); if (!snap.isEmpty()) break; QThread::msleep(50); }
        require(snap.value("stock_a3").toInt() == 3096 && snap.value("jobs").toArray().size() == 1, "stock and orders restored");
        accepted(client.request("set_paused", {{"paused", true}}), "pause reopened shop");
        QJsonArray longQueue;
        for (int i = 0; i < 257; ++i)
            longQueue.append(QJsonObject{{"job_id", QStringLiteral("horizon-%1").arg(i)}, {"title", "short order"}, {"pages", 1}, {"copies", 1}});
        accepted(client.request("submit_batch", {{"jobs", longQueue}}), "submit long queue");
        timer.restart();
        while (timer.elapsed() < 5000) {
            snap = client.request("snapshot");
            if (snap.value("jobs").toArray().last().toObject().value("estimate_blocked") == "horizon") break;
            QThread::msleep(30);
        }
        require(snap.value("blocked_orders").toInt() == 1 && snap.value("queue_clear_ms").isNull(),
                "a partial forecast must not promise whole-queue clearance");
        accepted(client.request("shutdown"), "stop reopened shop"); require(process.waitForFinished(5000), "stop reopened shop");
        std::cout << "Shop quote, order, device, stock, settlement and persistence workflow passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
