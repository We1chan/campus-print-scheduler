#include "cps/core/shop_replay.hpp"
#include <algorithm>
#include <stdexcept>
namespace cps {
ShopReplayResult replayShop(const std::vector<Job>& orders, Policy policy,
                           const std::vector<Printer>& fleet, TimeMs horizon, Inventory supplied) {
    if (!validPolicy(policy) || fleet.empty() || fleet.size() > 64 || horizon < 1 || horizon > MaxTimeMs ||
        supplied.a4 < 0 || supplied.a3 < 0) throw std::invalid_argument("invalid shop replay options");
    for (const auto& p : fleet) if (!validPrinter(p)) throw std::invalid_argument("invalid fleet");
    Scheduler scheduler;
    if (!scheduler.submitBatch(orders)) throw std::invalid_argument("invalid or duplicate orders");
    struct Running { Job job; TimeMs until; };
    std::vector<Running> active;
    ShopReplayResult result;
    TimeMs now = 0;
    while (true) {
        for (auto it = active.begin(); it != active.end();) {
            if (it->until <= now) {
                if (!scheduler.finish(it->job.id, it->job.attempts, true, now)) throw std::logic_error("completion failed");
                it = active.erase(it);
            } else ++it;
        }
        if (now >= horizon) break;
        std::vector<int> idle;
        for (int i = 0; i < static_cast<int>(fleet.size()); ++i) {
            if (std::none_of(active.begin(), active.end(), [i](const Running& r) { return r.job.assignedPrinter == i; })) idle.push_back(i);
        }
        while (!idle.empty()) {
            const auto j = scheduler.claimFor(policy, now, fleet, idle, supplied);
            if (!j) break;
            const auto end = now + processingMs(*j, fleet[j->assignedPrinter]);
            active.push_back({*j, end});
            result.dispatches.push_back({j->id, j->assignedPrinter, now, end});
            idle.erase(std::remove(idle.begin(), idle.end(), j->assignedPrinter), idle.end());
        }
        TimeMs next = horizon;
        for (const auto& r : active) next = std::min(next, r.until);
        for (const auto& j : orders) if (j.submittedMs > now) next = std::min(next, j.submittedMs);
        now = next;
    }
    result.jobs = scheduler.snapshot();
    // Future arrivals are not business demand within this observation window.
    result.jobs.erase(std::remove_if(result.jobs.begin(), result.jobs.end(), [horizon](const Job& j) {
        return j.submittedMs > horizon;
    }), result.jobs.end());
    result.business = shopMetrics(result.jobs, 0, horizon);
    result.performance = statistics(result.jobs, 0, horizon, static_cast<int>(fleet.size()));
    return result;
}
} // namespace cps
