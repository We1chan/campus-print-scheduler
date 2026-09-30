#include "cps/core/estimate.hpp"
#include <algorithm>
#include <stdexcept>

namespace cps {
std::vector<WaitEstimate> estimateWaits(const std::vector<Job>& jobs, TimeMs now, const EstimateOptions& o) {
    const bool shop = !o.fleet.empty();
    const int devices = shop ? static_cast<int>(o.fleet.size()) : o.printers;
    if (now < 0 || now > MaxTimeMs || !validPolicy(o.policy) || devices < 1 || devices > 64 ||
        o.pageMs < 1 || o.pageMs > 10000 || o.maxDispatches < 0 || o.supplied.a4 < 0 || o.supplied.a3 < 0)
        throw std::invalid_argument("invalid estimate options");
    for (const auto& p : o.fleet) if (!validPrinter(p)) throw std::invalid_argument("invalid fleet");

    // Finished orders only matter through the paper they consumed. A cancelling
    // order is about to release both its device and its reservation.
    std::vector<Job> live, settled;
    for (const auto& j : jobs) {
        if (j.state == JobState::Queued || j.state == JobState::Running) live.push_back(j);
        else if (j.state != JobState::Cancelling) settled.push_back(j);
    }
    auto supplied = availablePaper(settled, o.supplied);
    supplied.a4 = std::max(supplied.a4, 0); supplied.a3 = std::max(supplied.a3, 0);
    Scheduler sim(o.scheduler, [now] { return now; });
    if (!sim.restore(live, now)) throw std::invalid_argument("inconsistent snapshot for estimate");

    const auto duration = [&](const Job& j, int device) {
        return shop ? processingMs(j, o.fleet[static_cast<std::size_t>(device)]) : o.pageMs * j.sheets();
    };
    const auto after = [](TimeMs start, TimeMs length) { return length > MaxTimeMs - start ? MaxTimeMs : start + length; };
    struct Active { std::string id; int attempt; int device; TimeMs until; };
    std::vector<Active> active;
    std::vector<bool> busy(static_cast<std::size_t>(devices), false);
    std::vector<WaitEstimate> result;
    for (const auto& j : live) {
        if (j.state != JobState::Running) continue;
        int device = shop ? j.assignedPrinter : -1;
        if (!shop) for (int i = 0; i < devices && device < 0; ++i) if (!busy[static_cast<std::size_t>(i)]) device = i;
        if (device < 0 || device >= devices) continue; // no slot to model; leave it out of the forecast
        const auto done = std::find_if(o.printed.begin(), o.printed.end(), [&](const auto& p) { return p.first == j.id; });
        const int printed = done == o.printed.end() ? 0 : std::clamp(done->second, 0, j.sheets());
        const auto until = after(now, duration(j, device) * (j.sheets() - printed) / j.sheets());
        busy[static_cast<std::size_t>(device)] = true;
        active.push_back({j.id, j.attempts, device, until});
        result.push_back({j.id, WaitBlock::None, j.runs.back().startedMs, until, device, 0});
    }

    int dispatched = 0;
    for (TimeMs t = now;;) {
        for (auto it = active.begin(); it != active.end();) {
            if (it->until > t) { ++it; continue; }
            if (!sim.finish(it->id, it->attempt, true, t)) throw std::logic_error("estimate completion failed");
            busy[static_cast<std::size_t>(it->device)] = false;
            it = active.erase(it);
        }
        while (dispatched < o.maxDispatches) {
            std::vector<int> idle;
            for (int i = 0; i < devices; ++i) if (!busy[static_cast<std::size_t>(i)]) idle.push_back(i);
            if (idle.empty()) break;
            const auto j = shop ? sim.claimFor(o.policy, t, o.fleet, idle, supplied) : sim.claim(o.policy, t);
            if (!j) break;
            const int device = shop ? j->assignedPrinter : idle.front();
            const auto until = after(t, duration(*j, device));
            busy[static_cast<std::size_t>(device)] = true;
            active.push_back({j->id, j->attempts, device, until});
            result.push_back({j->id, WaitBlock::None, t, until, device, dispatched++});
        }
        if (active.empty() || dispatched >= o.maxDispatches) break;
        t = active.front().until;
        for (const auto& a : active) t = std::min(t, a.until);
    }

    const auto rest = sim.snapshot();
    const auto stock = availablePaper(rest, supplied);
    for (const auto& j : rest) {
        if (j.state != JobState::Queued) continue;
        auto reason = WaitBlock::Horizon;
        if (shop) {
            const bool device = std::any_of(o.fleet.begin(), o.fleet.end(), [&](const Printer& p) { return compatible(j, p); });
            if (!device) reason = WaitBlock::NoDevice;
            else if (j.attempts == 0 && j.paperSheets() > (j.paper == PaperSize::A3 ? stock.a3 : stock.a4)) reason = WaitBlock::NoPaper;
        }
        result.push_back({j.id, reason, 0, 0, -1, dispatched});
    }
    return result;
}
} // namespace cps
