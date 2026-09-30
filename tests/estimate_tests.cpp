#include "core_test_support.hpp"
#include "cps/core/estimate.hpp"
#include "cps/core/shop_replay.hpp"

using namespace cps;
using test::require;
using test::job;
namespace {
Job order(const std::string& id, int pages, bool color = false, PaperSize paper = PaperSize::A4, bool duplex = false) {
    Job j = job(id, pages);
    j.color = color; j.paper = paper; j.duplex = duplex;
    quote(j);
    return j;
}
const WaitEstimate& find(const std::vector<WaitEstimate>& all, const std::string& id) {
    for (const auto& e : all) if (e.id == id) return e;
    throw std::runtime_error("no estimate for " + id);
}
bool planned(const WaitEstimate& e, TimeMs start, TimeMs finish, int ahead) {
    return e.blocked == WaitBlock::None && e.startMs == start && e.finishMs == finish && e.ahead == ahead;
}

void homogeneousQueue() {
    const std::vector<Job> jobs{job("a", 10), job("b", 20), job("c", 5)};
    EstimateOptions o; o.pageMs = 100; o.printers = 2;
    auto e = estimateWaits(jobs, 0, o);
    require(e.size() == 3, "one estimate per waiting order");
    require(planned(find(e, "a"), 0, 1000, 0) && planned(find(e, "b"), 0, 2000, 1), "idle printers start at once");
    require(planned(find(e, "c"), 1000, 1500, 2), "third order waits for the first free printer");
    o.printers = 1; o.policy = Policy::ShortestJobFirst;
    e = estimateWaits(jobs, 0, o);
    require(planned(find(e, "c"), 0, 500, 0) && planned(find(e, "a"), 500, 1500, 1) &&
            planned(find(e, "b"), 1500, 3500, 2), "forecast follows the active policy");
    require(e[0].id == "c" && e[2].id == "b", "queued estimates are listed in forecast order");
    o.maxDispatches = 1;
    e = estimateWaits(jobs, 0, o);
    require(find(e, "c").blocked == WaitBlock::None && find(e, "a").blocked == WaitBlock::Horizon &&
            find(e, "b").blocked == WaitBlock::Horizon, "orders beyond the horizon stay unestimated");
    require(estimateWaits({}, 0, o).empty(), "empty queue");
}

void runningProgressAndSettledOrders() {
    Scheduler s;
    require(s.submit(job("run", 10)) && s.submit(job("next", 4)) && s.submit(job("stop", 3)) &&
            s.submit(job("done", 2)), "submit");
    require(s.claim(Policy::Fcfs, 0)->id == "run", "claim running order");
    EstimateOptions o; o.pageMs = 100; o.printers = 1;
    auto e = estimateWaits(s.snapshot(), 300, o);
    require(planned(find(e, "run"), 0, 1300, 0), "unknown progress assumes a full run from now");
    o.printed = {{"run", 5}, {"unknown", 9}};
    e = estimateWaits(s.snapshot(), 300, o);
    require(planned(find(e, "run"), 0, 800, 0), "remaining sheets shorten the running order");
    require(planned(find(e, "next"), 800, 1200, 0) && planned(find(e, "stop"), 1200, 1500, 1), "queue follows the running order");
    o.printed = {{"run", 99}};
    require(find(estimateWaits(s.snapshot(), 300, o), "next").startMs == 300, "overdue running order frees its device now");

    // Settled and cancelling orders are not forecast and do not hold a device.
    require(s.finish("run", 1, true, 400), "finish");
    require(s.claim(Policy::Fcfs, 400)->id == "next" && s.cancel("next", 450), "cancel a running order");
    require(s.cancel("done", 450), "cancel a queued order");
    o.printed.clear();
    e = estimateWaits(s.snapshot(), 500, o);
    require(e.size() == 1 && planned(find(e, "stop"), 500, 800, 0), "only live orders are estimated");
    const auto before = s.snapshot();
    estimateWaits(before, 500, o);
    require(s.snapshot().size() == 4 && s.snapshot()[2].state == JobState::Queued, "estimating never changes the real queue");
}

void shopMatchingAndBlocking() {
    auto fleet = defaultFleet(100);
    std::vector<Job> jobs{order("mono", 10), order("poster", 4, true, PaperSize::A3), order("color", 10, true)};
    EstimateOptions o; o.fleet = fleet; o.supplied = {100, 100};
    auto e = estimateWaits(jobs, 0, o);
    require(planned(find(e, "mono"), 0, 1000, 0) && find(e, "mono").printer == 0, "mono order takes the fastest device");
    require(find(e, "poster").printer == 5 && find(e, "poster").finishMs == processingMs(jobs[1], fleet[5]), "A3 routes to the capable device");
    require(find(e, "color").printer == 4, "colour order takes the fastest colour device");

    fleet[5].online = false; o.fleet = fleet;
    e = estimateWaits(jobs, 0, o);
    require(find(e, "poster").blocked == WaitBlock::NoDevice && find(e, "mono").blocked == WaitBlock::None,
            "an offline device blocks only the orders that need it");
    o.fleet = defaultFleet(100); o.supplied = {100, 3};
    require(find(estimateWaits(jobs, 0, o), "poster").blocked == WaitBlock::NoPaper, "paper shortage is reported");

    // Paper already consumed by completed orders counts against the supply.
    Scheduler s;
    require(s.submit(order("used", 95)) && s.submit(order("later", 10)), "submit stock orders");
    require(s.claimFor(Policy::Fcfs, 0, o.fleet, {0}, {100, 0}).value().id == "used" && s.finish("used", 1, true, 9500), "consume paper");
    o.supplied = {100, 0};
    require(find(estimateWaits(s.snapshot(), 9500, o), "later").blocked == WaitBlock::NoPaper, "consumed stock is not reused");
    o.supplied = {105, 0};
    require(planned(find(estimateWaits(s.snapshot(), 9500, o), "later"), 9500, 10500, 0), "restock unblocks the order");
}

// With no new arrivals the forecast must equal what the scheduler really does.
void forecastMatchesReplay() {
    std::vector<Job> orders;
    for (int i = 0; i < 40; ++i) {
        auto j = order("o" + std::to_string(i), 3 + (i * 17) % 60, i % 4 == 1 || i % 4 == 2,
                       i % 4 == 2 ? PaperSize::A3 : PaperSize::A4, i % 4 == 0);
        j.copies = 1 + i % 2; j.service = static_cast<ServiceTier>(i % 3); j.priority = (i * 7) % 20;
        quote(j);
        orders.push_back(j);
    }
    const auto fleet = defaultFleet(100);
    for (const auto policy : {Policy::Fcfs, Policy::ShortestJobFirst, Policy::PriorityAging, Policy::Deadline, Policy::Revenue}) {
        const auto replay = replayShop(orders, policy, fleet, 10000000);
        EstimateOptions o; o.policy = policy; o.fleet = fleet; o.supplied = {1000000, 1000000};
        const auto e = estimateWaits(orders, 0, o);
        require(replay.dispatches.size() == orders.size() && e.size() == orders.size(), "every order is dispatched");
        for (std::size_t i = 0; i < replay.dispatches.size(); ++i) {
            const auto& d = replay.dispatches[i];
            require(e[i].id == d.jobId && e[i].printer == d.printer && e[i].startMs == d.startedMs &&
                    e[i].finishMs == d.finishedMs && e[i].ahead == static_cast<int>(i), "forecast equals replayed dispatch");
        }
    }
}

void invalidInput() {
    EstimateOptions o;
    test::throws<std::invalid_argument>([&] { estimateWaits({}, -1, o); }, "negative time");
    o.printers = 0;
    test::throws<std::invalid_argument>([&] { estimateWaits({}, 0, o); }, "no devices");
    o.printers = 2;
    auto broken = job("bad", 1, 50);
    test::throws<std::invalid_argument>([&] { estimateWaits({broken}, 10, o); }, "order submitted after now");
    test::throws<std::invalid_argument>([&] { estimateWaits({job("dup"), job("dup")}, 0, o); }, "duplicate ids");
}
}

int main() {
    try {
        homogeneousQueue();
        runningProgressAndSettledOrders();
        shopMatchingAndBlocking();
        forecastMatchesReplay();
        invalidInput();
        std::cout << "Wait estimates for queues, progress, devices, stock and policies passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
