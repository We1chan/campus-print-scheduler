#pragma once
#include "cps/core/shop.hpp"
#include <utility>

namespace cps {
enum class WaitBlock { None, NoDevice, NoPaper, Horizon };
struct WaitEstimate {
    std::string id;
    WaitBlock blocked = WaitBlock::None;
    TimeMs startMs = 0;  // forecast dispatch; the recorded start for a running order
    TimeMs finishMs = 0;
    int printer = -1;
    int ahead = 0;       // forecast dispatches that happen before this order
};
struct PrintProgress {
    std::string id;
    int printed = 0;
    TimeMs atMs = 0;  // when this progress was observed, on the scheduler timeline
};
struct EstimateOptions {
    Policy policy = Policy::Fcfs;
    SchedulerOptions scheduler = {};
    std::vector<Printer> fleet = {};  // empty: homogeneous queue of `printers` devices
    Inventory supplied = {};
    int printers = 2;
    TimeMs pageMs = 150;              // homogeneous queue only
    std::vector<PrintProgress> printed = {}; // latest progress, including its observation time
    int maxDispatches = 256;          // bounds the cost on very long queues
};
// Forecast for every Queued and Running order at `now`. It replays the real claim
// rules on a copy, so it follows the active policy, device matching and paper stock.
// It is an estimate, not a promise: it assumes dispatch is not paused and there are
// no new arrivals, failures, cancellations or configuration changes.
// Running orders come first, then queued orders in forecast order, then blocked ones.
std::vector<WaitEstimate> estimateWaits(const std::vector<Job>& jobs, TimeMs now,
                                        const EstimateOptions& options);
} // namespace cps
