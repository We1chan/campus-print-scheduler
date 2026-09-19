#pragma once
#include "cps/core/scheduler.hpp"
#include "cps/metrics/statistics.hpp"

namespace cps {
struct ReplayOptions {
    int printers = 2;
    TimeMs pageMs = 150;
    TimeMs agingIntervalMs = 30000;
};
struct Dispatch {
    std::string jobId;
    int printer = 0; // zero-based, stable tie ordering
    TimeMs startedMs = 0;
    TimeMs finishedMs = 0;
};
struct ReplayResult {
    TimeMs startedMs = 0;
    TimeMs finishedMs = 0;
    Statistics metrics;
    std::vector<Job> jobs;
    std::vector<Dispatch> dispatches;
};
// Deterministic, no real sleeping or processes; equal-time completions and
// arrivals are all processed before assigning the next jobs to idle printers.
ReplayResult replay(std::vector<Job> workload, Policy policy, ReplayOptions options = {});
} // namespace cps
