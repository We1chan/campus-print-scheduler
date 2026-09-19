#include "cps/core/replay.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace cps {
ReplayResult replay(std::vector<Job> workload, Policy policy, ReplayOptions options) {
    if (options.printers < 1 || options.printers > 64 || options.pageMs < 1 ||
        options.pageMs > MaxTimeMs / 10000 ||
        (policy != Policy::Fcfs && policy != Policy::ShortestJobFirst && policy != Policy::PriorityAging))
        throw std::invalid_argument("invalid replay options");
    TimeMs now = 0;
    Scheduler scheduler({options.agingIntervalMs}, [&now] { return now; });
    ReplayResult result;
    std::stable_sort(workload.begin(), workload.end(), [](const Job& a, const Job& b) {
        return a.submittedMs < b.submittedMs;
    });
    struct Slot { std::optional<Job> job; TimeMs until = 0; };
    std::vector<Slot> slots(static_cast<std::size_t>(options.printers));
    std::size_t nextArrival = 0, completed = 0;
    if (!workload.empty()) now = workload.front().submittedMs;
    result.startedMs = now;
    while (completed < workload.size()) {
        for (auto& slot : slots) {
            if (slot.job && slot.until == now) {
                if (!scheduler.finish(slot.job->id, slot.job->attempts, true, now))
                    throw std::logic_error("replay completion rejected");
                slot.job.reset();
                ++completed;
            }
        }
        while (nextArrival < workload.size() && workload[nextArrival].submittedMs <= now) {
            if (!scheduler.submit(workload[nextArrival++]))
                throw std::invalid_argument("workload contains an invalid or duplicate job");
        }
        for (std::size_t i = 0; i < slots.size(); ++i) {
            auto& slot = slots[i];
            if (slot.job) continue;
            slot.job = scheduler.claim(policy, now);
            if (slot.job) {
                const auto duration = options.pageMs * slot.job->sheets();
                if (now > MaxTimeMs - duration) throw std::overflow_error("replay timeline is too long");
                slot.until = now + duration;
                result.dispatches.push_back({slot.job->id, static_cast<int>(i), now, slot.until});
            }
        }
        if (completed == workload.size()) break;
        TimeMs next = std::numeric_limits<TimeMs>::max();
        if (nextArrival < workload.size()) next = workload[nextArrival].submittedMs;
        for (const auto& slot : slots) if (slot.job) next = std::min(next, slot.until);
        if (next <= now || next > MaxTimeMs) throw std::logic_error("replay cannot advance");
        now = next;
    }
    result.finishedMs = now;
    result.jobs = scheduler.snapshot();
    result.metrics = statistics(result.jobs, result.startedMs, now, options.printers);
    return result;
}
} // namespace cps
