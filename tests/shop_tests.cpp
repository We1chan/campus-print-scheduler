#include "cps/core/shop.hpp"
#include "cps/core/shop_replay.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
cps::Job order(const char* id, int pages = 10) {
    cps::Job j; j.id = id; j.title = id; j.pages = pages; cps::quote(j); return j;
}
}
int main() {
    try {
        auto j = order("duplex", 3); j.copies = 2; j.duplex = true; j.service = cps::ServiceTier::Flexible;
        cps::quote(j);
        check(j.sheets() == 6 && j.paperSheets() == 4, "odd duplex copies use separate sheets");
        check(j.baseCents == 120 && j.quotedCents == 102, "quote uses integer cents and locked tier");
        auto fleet = cps::defaultFleet(100);
        j.color = true; j.paper = cps::PaperSize::A3;
        check(!cps::compatible(j, fleet[0]) && cps::compatible(j, fleet[5]), "capability matching");
        cps::Scheduler s;
        j = order("a3"); j.paper = cps::PaperSize::A3; cps::quote(j);
        check(s.submit(j), "submit A3");
        check(!s.claimFor(cps::Policy::Fcfs, 0, fleet, {0, 1}, {100, 100}), "incompatible devices stay idle");
        check(!s.claimFor(cps::Policy::Fcfs, 0, fleet, {5}, {100, 9}), "insufficient inventory blocks dispatch");
        auto run = s.claimFor(cps::Policy::Fcfs, 0, fleet, {5}, {100, 10});
        check(run && run->assignedPrinter == 5, "A3 routes to capable device");
        check(cps::availablePaper(s.snapshot(), {100, 10}).a3 == 0, "reservation counted once");
        check(s.retry(j.id, 1, 5), "retry");
        run = s.claimFor(cps::Policy::Revenue, 6, fleet, {5}, {100, 10});
        check(run && run->attempts == 2, "retry reuses reserved sheets");
        check(s.finish(j.id, 2, true, 10), "complete");
        check(s.refund(j.id, 100) && s.refund(j.id, 100), "refund uses idempotent cumulative amount");
        check(!s.refund(j.id, 99) && !s.refund(j.id, 100000), "refund bounds");
        const auto m = cps::shopMetrics(s.snapshot(), 0, 10);
        check(m.completed == 1 && m.grossCents == j.quotedCents && m.revenueCents == j.quotedCents - 100,
              "only completed orders settle once");
        cps::Scheduler restored;
        check(restored.restore(s.snapshot(), 10), "commercial state restores");
        check(cps::shopMetrics(restored.snapshot(), 0, 10).revenueCents == m.revenueCents, "restart does not double settle");
        cps::Scheduler concurrent;
        check(concurrent.submit(order("one")) && concurrent.submit(order("two")), "concurrent submissions");
        std::optional<cps::Job> a, b;
        std::thread t1([&] { a = concurrent.claimFor(cps::Policy::Fcfs, 0, fleet, {0}, {10, 0}); });
        std::thread t2([&] { b = concurrent.claimFor(cps::Policy::Fcfs, 0, fleet, {0, 1}, {10, 0}); });
        t1.join(); t2.join();
        check(bool(a) != bool(b), "concurrent claims cannot oversell inventory");
        const auto claimed = a ? *a : *b;
        check(concurrent.cancel(claimed.id, 1) && concurrent.acknowledgeCancel(claimed.id, 1, 2), "cancel execution");
        check(cps::availablePaper(concurrent.snapshot(), {10, 0}).a4 == 10, "cancel releases reservation");
        check(concurrent.claimFor(cps::Policy::Fcfs, 3, fleet, {0, 1}, {10, 0}).has_value(), "released stock reusable");
        cps::Scheduler paying;
        auto online = order("online"); online.payment = cps::PaymentMethod::Wechat; online.customer = "2024000001";
        check(paying.submit(online) && paying.submit(order("counter")), "paid orders");
        auto money = cps::shopMetrics(paying.snapshot(), 0, 10);
        check(money.prepaidCents == online.quotedCents && money.pendingCents == 2 * online.quotedCents,
              "online prepayment is held until the order completes");
        check(paying.claimFor(cps::Policy::Fcfs, 0, fleet, {0}, {100, 0}).value().id == "online" &&
              paying.finish("online", 1, true, 5) && paying.refund("online", 50), "complete paid order");
        money = cps::shopMetrics(paying.snapshot(), 0, 10);
        check(money.prepaidCents == 0 && money.paidCents[1] == online.quotedCents - 50 && money.paidCents[0] == 0,
              "settled revenue is split by payment channel");
        check(paying.snapshot()[0].customer == "2024000001", "customer survives the queue");
        online.id = "bad-channel"; online.payment = static_cast<cps::PaymentMethod>(9);
        check(!paying.submit(online), "unknown payment method rejected");
        cps::Scheduler deadlines;
        auto slow = order("later", 100), quick = order("urgent", 10);
        slow.deadlineMs = 100000; quick.deadlineMs = 1000;
        check(deadlines.submit(slow) && deadlines.submit(quick), "deadline orders");
        check(deadlines.claimFor(cps::Policy::Deadline, 0, fleet, {0}, {}).value().id == "urgent", "deadline priority");
        cps::Scheduler revenue;
        slow = order("mono"); quick = order("color"); quick.color = true; cps::quote(quick);
        check(revenue.submit(slow) && revenue.submit(quick), "revenue orders");
        check(revenue.claimFor(cps::Policy::Revenue, 0, fleet, {4}, {}).value().id == "color", "revenue per device time");
        fleet[5].online = false;
        check(!cps::compatible(j, fleet[5]), "offline devices excluded");
        auto first = order("close-first", 10), second = order("close-second", 10);
        const std::vector<cps::Printer> single{fleet[0]};
        const auto replay = cps::replayShop({first, second}, cps::Policy::Fcfs, single, 1000);
        check(replay.business.completed == 1 && replay.business.grossCents == first.quotedCents, "completion at closing counts, queued orders earn zero");
        check(replay.dispatches.size() == 1, "no dispatch at closing");
        const auto partial = cps::replayShop({first}, cps::Policy::Fcfs, single, 500);
        check(partial.business.completed == 0 && partial.business.revenueCents == 0, "incomplete order has no revenue");
        check(cps::replayShop({first}, cps::Policy::Fcfs, single, 1000, {0, 0}).dispatches.empty(), "stock-blocked replay terminates");
        std::cout << "Shop pricing, matching, stock, settlement, concurrency and policies passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
