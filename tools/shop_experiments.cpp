#include "cps/core/shop_replay.hpp"
#include "cps/runtime/shop_io.hpp"
#include "cps/runtime/state_store.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

namespace {
qint64 number(const QJsonObject& o, const char* key, qint64 low, qint64 high) {
    const auto value = o.value(QLatin1String(key));
    const auto n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < low || n > high)
        throw std::invalid_argument(std::string("invalid workload field: ") + key);
    return static_cast<qint64>(n);
}
std::vector<cps::Job> readOrders(const QString& path, QString& digest) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 64*1024*1024)
        throw std::invalid_argument("cannot read workload or workload exceeds 64 MiB");
    const auto bytes = input.readAll();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto root = document.object();
    if (input.error() != QFileDevice::NoError || error.error != QJsonParseError::NoError || !document.isObject() ||
        number(root, "schema_version", 1, 1) != 1 || root.value("kind") != "campus-print-workload" ||
        !root.value("jobs").isArray() || root.value("jobs").toArray().isEmpty() || root.value("jobs").toArray().size() > 2000)
        throw std::invalid_argument("expected 1..2000 orders in campus-print-workload schema 1");
    std::vector<cps::Job> orders;
    const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
    for (const auto& row : root.value("jobs").toArray()) {
        if (!row.isObject()) throw std::invalid_argument("each workload order must be an object");
        const auto o = row.toObject();
        const auto id = o.value("job_id").toString(), title = o.value("title").toString();
        if (!safeId.match(id).hasMatch() || title.trimmed().isEmpty() || title.size() > 200)
            throw std::invalid_argument("invalid workload job identity");
        cps::Job j; j.id = id.toStdString(); j.title = title.toUtf8().toStdString();
        j.pages = int(number(o, "pages", 1, 1000)); j.copies = int(number(o, "copies", 1, 100));
        j.submittedMs = number(o, "submitted_ms", 0, cps::MaxTimeMs);
        if (o.contains("priority")) j.priority = int(number(o, "priority", 0, 100));
        if (!cps::orderTermsFromJson(o, j)) throw std::invalid_argument("invalid workload order terms");
        cps::quote(j); // prices and promises always come from the production rules
        orders.push_back(j);
    }
    digest = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return orders;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser cli; cli.addHelpOption();
    cli.addOptions({{"output", "JSON report path", "file", "shop-experiments.json"},
        {"seed", "Deterministic seed", "n", "20260923"}, {"customers", "Potential customers (1..2000)", "n", "600"},
        {"hours", "Trading horizon (1..12 hours)", "n", "8"},
        {"input", "Fixed document-derived workload JSON; compare FCFS, SJF and balanced", "file"}});
    cli.process(app);
    bool okSeed, okCount, okHours;
    const auto seed = cli.value("seed").toUInt(&okSeed); int count = cli.value("customers").toInt(&okCount);
    const int hours = cli.value("hours").toInt(&okHours);
    if (!okSeed || !okCount || !okHours || count < 1 || count > 2000 || hours < 1 || hours > 12) return 64;
    try {
        const cps::TimeMs horizon = cps::TimeMs(hours) * 3600000;
        const auto fleet = cps::defaultFleet();
        const bool external = cli.isSet("input");
        QString workloadDigest;
        std::vector<cps::Job> fixed;
        if (external) {
            if (QFileInfo(cli.value("input")).absoluteFilePath() == QFileInfo(cli.value("output")).absoluteFilePath())
                throw std::invalid_argument("output must not replace input");
            fixed = readOrders(cli.value("input"), workloadDigest);
            if (std::any_of(fixed.begin(), fixed.end(), [horizon](const auto& j) { return j.submittedMs > horizon; }))
                throw std::invalid_argument("workload arrivals must not be after closing");
            count = int(fixed.size());
        }
        // mt19937 raw draws + modulo provide the same generated customers across standard libraries.
        std::mt19937 random(seed);
        struct Customer { cps::Job order; int budgetRank; cps::TimeMs patience; };
        std::vector<Customer> customers;
        QJsonArray demand;
        for (int i = 0; !external && i < count; ++i) {
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
        const std::vector<cps::Policy> policies{cps::Policy::Fcfs, cps::Policy::ShortestJobFirst, cps::Policy::Balanced};
        auto record = [&](const char* mode, const std::vector<cps::Job>& orders, cps::Policy policy, int share, bool discount) {
            const auto r = cps::replayShop(orders, policy, fleet, horizon);
            auto row = cps::shopMetricsToJson(r.business);
            row.insert("mode", mode); row.insert("policy", cps::policyName(policy)); row.insert("discounts", discount);
            row.insert("budget_sensitive_percent", share); row.insert("potential_customers", count);
            row.insert("accepted", int(orders.size())); row.insert("lost_before_order", count - int(orders.size()));
            row.insert("unfinished_at_close", int(orders.size()) - r.business.completed);
            row.insert("average_first_wait_ms", r.performance.averageFirstWaitMs); row.insert("utilization", r.performance.utilization);
            row.insert("max_first_wait_ms", static_cast<qint64>(r.performance.maxFirstWaitMs));
            row.insert("first_started", int(r.performance.firstStarted));
            std::vector<cps::TimeMs> waits;
            long double observedWait = 0;
            for (const auto& j : r.jobs)
                observedWait += j.firstStartedMs.value_or(horizon) - j.submittedMs;
            row.insert("average_observed_wait_ms", r.jobs.empty() ? 0.0 : double(observedWait/r.jobs.size()));
            for (const auto& j : r.jobs) if (j.firstStartedMs) waits.push_back(*j.firstStartedMs - j.submittedMs);
            std::sort(waits.begin(), waits.end());
            row.insert("p95_first_wait_ms", waits.empty() ? 0.0 : double(waits[(waits.size()*95 + 99)/100 - 1]));
            const auto quality = cps::shopQuality(r.jobs, 0, horizon);
            row.insert("p95_observed_wait_ms", quality.p95ObservedWaitMs);
            row.insert("express_accepted", quality.expressAccepted);
            row.insert("express_on_time", quality.expressOnTime);
            row.insert("express_on_time_rate", quality.expressOnTimeRate);
            row.insert("potential_contribution_cents", double(quality.potentialContributionCents));
            row.insert("waiting_score", quality.waitingScore);
            row.insert("contribution_score", quality.contributionScore);
            row.insert("quality_score", quality.overallScore);
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
        for (const auto& c : customers) {
            auto j = c.order; j.service = static_cast<cps::ServiceTier>((c.budgetRank / 10) % 3); cps::quote(j); fixed.push_back(j);
        }
        if (external) for (const auto& j : fixed) demand.append(cps::jobSnapshotToJson(j));
        for (const auto policy : policies) record("fixed_orders", fixed, policy, 0, true);
        if (!external) for (const int share : {0, 30, 60}) for (bool discount : {false, true}) {
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
            {"evaluation", QJsonObject{{"name", "balanced-shop-quality-v1"}, {"wait_reference_ms", 1800000},
                {"waiting_weight", 1}, {"express_weight", 1}, {"contribution_weight", 1},
                {"note", "Equal weights; observed mean/P95 waiting includes unstarted orders through closing; express completion by promise; completed contribution divided by the cohort's nominal potential contribution. Same cohort and horizon required."}}},
            {"workload_sha256", workloadDigest},
            {"assumptions", external ? "Fixed document-derived workload; print options and arrival times are simulated, not observed orders. No customer-choice model, downtime or late refund. Same inputs and practically unlimited paper for every policy. Revenue only counts completed orders by closing. Costs exclude rent, labor, waste and incomplete work."
                : "Synthetic demand, not observed print-shop data. Customers select cheapest affordable tier within patience; ties choose shortest promise. Sensitive budgets are 90% of base. No abandonment after acceptance, no arrival response, no downtime, no late refund, unlimited practical paper stock. Revenue is completed settled orders by closing; running jobs earn zero. Costs exclude rent, labor, waste and incomplete work. This is a heuristic comparison, not proof of global optimality."},
            {"fleet", devices}, {"customers", demand}, {"runs", runs}};
        QSaveFile out(cli.value("output")); const auto bytes = QJsonDocument(report).toJson();
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) return 73;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 64; }
    return 0;
}
