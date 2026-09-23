#include "cps/core/shop_replay.hpp"
#include "cps/runtime/shop_io.hpp"
#include "cps/runtime/state_store.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <iostream>
#include <random>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser cli; cli.addHelpOption();
    cli.addOptions({{"output", "JSON report path", "file", "shop-experiments.json"},
        {"seed", "Deterministic seed", "n", "20260923"}, {"customers", "Potential customers (1..2000)", "n", "600"},
        {"hours", "Trading horizon (1..12 hours)", "n", "8"}});
    cli.process(app);
    bool okSeed, okCount, okHours;
    const auto seed = cli.value("seed").toUInt(&okSeed); const int count = cli.value("customers").toInt(&okCount);
    const int hours = cli.value("hours").toInt(&okHours);
    if (!okSeed || !okCount || !okHours || count < 1 || count > 2000 || hours < 1 || hours > 12) return 64;
    try {
        const cps::TimeMs horizon = cps::TimeMs(hours) * 3600000;
        const auto fleet = cps::defaultFleet();
        // mt19937 raw draws + modulo provide the same generated customers across standard libraries.
        std::mt19937 random(seed);
        struct Customer { cps::Job order; int budgetRank; cps::TimeMs patience; };
        std::vector<Customer> customers;
        QJsonArray demand;
        for (int i = 0; i < count; ++i) {
            cps::Job j; j.id = "customer-" + std::to_string(i + 1); j.title = j.id;
            j.pages = 10 + int(random() % 191); j.copies = 1 + int(random() % 3);
            j.submittedMs = random() % std::max<cps::TimeMs>(1, horizon / 2);
            j.paper = random() % 10 < 2 ? cps::PaperSize::A3 : cps::PaperSize::A4;
            j.color = random() % 10 < 3; j.duplex = random() % 2; j.service = cps::ServiceTier::Express;
            cps::quote(j);
            const int rank = int(random() % 100); const int patience = int(random() % 3);
            customers.push_back({j, rank, cps::serviceWindowMs(static_cast<cps::ServiceTier>(patience))});
            auto row = cps::jobSnapshotToJson(j); row.insert("budget_rank", rank);
            row.insert("max_wait_ms", double(customers.back().patience)); demand.append(row);
        }
        QJsonArray runs;
        const std::vector<cps::Policy> policies{cps::Policy::Fcfs, cps::Policy::Deadline, cps::Policy::Revenue};
        auto record = [&](const char* mode, const std::vector<cps::Job>& orders, cps::Policy policy, int share, bool discount) {
            const auto r = cps::replayShop(orders, policy, fleet, horizon);
            auto row = cps::shopMetricsToJson(r.business);
            row.insert("mode", mode); row.insert("policy", cps::policyName(policy)); row.insert("discounts", discount);
            row.insert("budget_sensitive_percent", share); row.insert("potential_customers", count);
            row.insert("accepted", int(orders.size())); row.insert("lost_before_order", count - int(orders.size()));
            row.insert("unfinished_at_close", int(orders.size()) - r.business.completed);
            row.insert("average_first_wait_ms", r.performance.averageFirstWaitMs); row.insert("utilization", r.performance.utilization);
            QJsonArray jobs, dispatch;
            for (const auto& j : r.jobs) jobs.append(cps::jobSnapshotToJson(j));
            for (const auto& d : r.dispatches) dispatch.append(QJsonObject{{"job_id", QString::fromStdString(d.jobId)},
                {"printer", d.printer}, {"started_ms", double(d.startedMs)}, {"finished_ms", double(d.finishedMs)}});
            row.insert("jobs", jobs); row.insert("dispatches", dispatch); runs.append(row);
            std::cout << mode << " / " << cps::policyName(policy).toStdString() << " / sensitive=" << share
                      << " / discounts=" << discount << ": accepted=" << orders.size()
                      << ", completed=" << r.business.completed << ", revenue_cents=" << r.business.revenueCents
                      << ", late=" << r.business.late << '\n';
        };
        std::vector<cps::Job> fixed;
        for (const auto& c : customers) {
            auto j = c.order; j.service = static_cast<cps::ServiceTier>((c.budgetRank / 10) % 3); cps::quote(j); fixed.push_back(j);
        }
        for (const auto policy : policies) record("fixed_orders", fixed, policy, 0, true);
        for (const int share : {0, 30, 60}) for (bool discount : {false, true}) {
            std::vector<cps::Job> accepted;
            for (const auto& c : customers) {
                const auto budget = c.budgetRank < share ? (c.order.baseCents * 90) / 100 : c.order.baseCents;
                std::optional<cps::Job> chosen;
                for (auto tier : {cps::ServiceTier::Express, cps::ServiceTier::Standard, cps::ServiceTier::Flexible}) {
                    auto j = c.order; j.service = tier; cps::quote(j); if (!discount) j.quotedCents = j.baseCents;
                    if (j.quotedCents > budget || cps::serviceWindowMs(tier) > c.patience) continue;
                    if (!chosen || j.quotedCents < chosen->quotedCents) chosen = j;
                }
                if (chosen) accepted.push_back(*chosen);
            }
            for (const auto policy : policies) record("customer_choice", accepted, policy, share, discount);
        }
        QJsonArray devices; for (const auto& p : fleet) devices.append(cps::printerToJson(p));
        const QJsonObject report{{"schema_version", 1}, {"seed", double(seed)}, {"horizon_ms", double(horizon)},
            {"assumptions", "Synthetic demand, not observed print-shop data. Customers select cheapest affordable tier within patience; ties choose shortest promise. Sensitive budgets are 90% of base. No abandonment after acceptance, no arrival response, no downtime, no late refund, unlimited practical paper stock. Revenue is completed settled orders by closing; running jobs earn zero. Costs exclude rent, labor, waste and incomplete work. This is a heuristic comparison, not proof of global optimality."},
            {"fleet", devices}, {"customers", demand}, {"runs", runs}};
        QSaveFile out(cli.value("output")); const auto bytes = QJsonDocument(report).toJson();
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) return 73;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 64; }
    return 0;
}
