#pragma once
#include "cps/core/scheduler.hpp"
#include <cstddef>

namespace cps {
struct Statistics {
    std::size_t submitted = 0;
    std::size_t firstStarted = 0;
    std::size_t completed = 0;
    std::size_t failed = 0;
    std::size_t cancelled = 0;
    std::size_t queued = 0;
    std::size_t running = 0;
    std::size_t cancelling = 0;
    double averageFirstWaitMs = 0;
    TimeMs maxFirstWaitMs = 0;
    double averageTotalQueueWaitMs = 0;
    double averageTurnaroundMs = 0;
    TimeMs maxCurrentQueueWaitMs = 0;
    double busyPrinterMs = 0;
    double throughputPerSecond = 0;
    double utilization = 0;
};
// Snapshot must describe windowEnd. Event cohorts include both boundaries;
// busy intervals are clipped to [windowStart, windowEnd). See interfaces.md.
Statistics statistics(const std::vector<Job>& jobs, TimeMs windowStart,
                      TimeMs windowEnd, int printers);
} // namespace cps
