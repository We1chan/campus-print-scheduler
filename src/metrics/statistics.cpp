#include "cps/metrics/statistics.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace cps {
Statistics statistics(const std::vector<Job>& jobs, TimeMs start, TimeMs end, int printers) {
    if (start < 0 || end < start || end > MaxTimeMs || printers < 1)
        throw std::invalid_argument("invalid statistics window or printer count");
    Statistics result;
    long double firstWait = 0, totalWait = 0, turnaround = 0, busy = 0;
    std::vector<std::pair<TimeMs, int>> occupancy;
    const auto inWindow = [&](TimeMs time) { return time >= start && time <= end; };
    for (const auto& job : jobs) {
        if (job.submittedMs > end) continue;
        if (job.lastChangedMs > end)
            throw std::invalid_argument("snapshot contains changes after statistics window");
        if (inWindow(job.submittedMs)) ++result.submitted;
        if (job.firstStartedMs && inWindow(*job.firstStartedMs)) {
            const auto wait = *job.firstStartedMs - job.submittedMs;
            firstWait += wait;
            result.maxFirstWaitMs = std::max(result.maxFirstWaitMs, wait);
            ++result.firstStarted;
        }
        if (job.finishedMs && inWindow(*job.finishedMs)) {
            if (job.state == JobState::Completed) {
                ++result.completed;
                totalWait += job.accumulatedWaitMs;
                turnaround += *job.finishedMs - job.submittedMs;
            } else if (job.state == JobState::Failed) {
                ++result.failed;
            } else if (job.state == JobState::Cancelled) {
                ++result.cancelled;
            }
        }
        if (job.state == JobState::Queued) {
            ++result.queued;
            result.maxCurrentQueueWaitMs = std::max(result.maxCurrentQueueWaitMs,
                job.accumulatedWaitMs + end - job.queuedSinceMs);
        } else if (job.state == JobState::Running) {
            ++result.running;
        } else if (job.state == JobState::Cancelling) {
            ++result.cancelling;
        }
        for (const auto& run : job.runs) {
            const auto from = std::max(start, run.startedMs);
            const auto to = std::min(end, run.stoppedMs.value_or(end));
            if (to > from) {
                busy += to - from;
                occupancy.emplace_back(from, 1);
                occupancy.emplace_back(to, -1);
            }
        }
    }
    std::sort(occupancy.begin(), occupancy.end()); // release before claim at the same time
    int occupied = 0;
    for (const auto& event : occupancy) {
        occupied += event.second;
        if (occupied > printers) throw std::invalid_argument("history exceeds printer capacity");
    }
    if (result.firstStarted)
        result.averageFirstWaitMs = static_cast<double>(firstWait / result.firstStarted);
    if (result.completed) {
        result.averageTotalQueueWaitMs = static_cast<double>(totalWait / result.completed);
        result.averageTurnaroundMs = static_cast<double>(turnaround / result.completed);
    }
    result.busyPrinterMs = static_cast<double>(busy);
    if (end > start) {
        result.throughputPerSecond = static_cast<double>(result.completed) * 1000.0 /
                                    static_cast<double>(end - start);
        result.utilization = static_cast<double>(busy / (end - start) / printers);
    }
    return result;
}
} // namespace cps
