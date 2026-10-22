#include "cps/core/scheduler.hpp"
#include "cps/core/shop.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace cps {
namespace {
bool validTime(TimeMs time) { return time >= 0 && time <= MaxTimeMs; }
bool validJob(const Job& job) {
    return !job.id.empty() && !job.title.empty() && job.pages >= 1 && job.pages <= 1000 &&
        job.copies >= 1 && job.copies <= 100 && job.pages * job.copies <= 10000 &&
        validTime(job.submittedMs) && job.priority >= 0 && job.priority <= 100 &&
        job.maxAttempts >= 1 && job.maxAttempts <= 100 && validTerms(job);
}
bool active(const Job& job, int attempt, TimeMs now) {
    return validTime(now) && now >= job.lastChangedMs && attempt > 0 &&
        attempt == job.attempts && !job.runs.empty() && !job.runs.back().stoppedMs;
}
bool validSnapshot(const Job& job, TimeMs now) {
    if (!validJob(job) || job.submittedMs > now || job.lastChangedMs < job.submittedMs ||
        job.lastChangedMs > now || job.attempts < 0 || job.attempts > job.maxAttempts ||
        static_cast<std::size_t>(job.attempts) != job.runs.size()) return false;
    if (job.attempts == 0 && (job.assignedPrinter != -1 || job.costCents != 0)) return false;
    TimeMs previous = job.submittedMs;
    TimeMs wait = 0;
    TimeMs lastQueueEntry = job.submittedMs;
    bool open = false;
    for (std::size_t i = 0; i < job.runs.size(); ++i) {
        const auto& run = job.runs[i];
        if (open || run.startedMs < previous || run.startedMs > job.lastChangedMs) return false;
        lastQueueEntry = previous;
        wait += run.startedMs - previous;
        if (run.stoppedMs) {
            if (*run.stoppedMs < run.startedMs || *run.stoppedMs > job.lastChangedMs) return false;
            previous = *run.stoppedMs;
        } else {
            open = true;
            if (i + 1 != job.runs.size()) return false;
            previous = run.startedMs;
        }
    }
    if (job.firstStartedMs != (job.runs.empty() ? std::optional<TimeMs>{} :
                               std::optional<TimeMs>{job.runs.front().startedMs})) return false;
    switch (job.state) {
    case JobState::Queued:
        if (open || job.finishedMs || job.attempts >= job.maxAttempts ||
            job.queuedSinceMs != previous || job.lastChangedMs != previous) return false;
        break;
    case JobState::Running:
        if (job.lastChangedMs != previous) return false;
        [[fallthrough]];
    case JobState::Cancelling:
        if (!open || job.finishedMs || job.queuedSinceMs != lastQueueEntry) return false;
        break;
    case JobState::Completed:
    case JobState::Failed:
        if (job.runs.empty() || open || !job.finishedMs || *job.finishedMs != previous ||
            job.lastChangedMs != previous || job.queuedSinceMs != lastQueueEntry) return false;
        break;
    case JobState::Cancelled:
        if (open || !job.finishedMs || *job.finishedMs < previous ||
            *job.finishedMs != job.lastChangedMs) return false;
        // A cancelled job either left the waiting queue, or stopped its last run.
        if (job.queuedSinceMs != previous &&
            (job.queuedSinceMs != lastQueueEntry || *job.finishedMs != previous)) return false;
        wait += *job.finishedMs - previous;
        break;
    default: return false;
    }
    return job.accumulatedWaitMs == wait;
}

// Everything the queue comparison needs for one decision at a fixed `now`.
// Built once per waiting job, so sorting compares stored integers instead of
// re-evaluating the Balanced score inside every comparison.
struct QueueKey {
    std::size_t index; // position in the scheduler's job list
    int sheets;
    TimeMs queuedSinceMs;
    TimeMs waitedMs;   // accumulated queue wait at `now`, kept across retries
    TimeMs score;      // Balanced weighted value; zero for FCFS and SJF
};
TimeMs balancedScore(const Job& job, TimeMs waited, TimeMs now, TimeMs agingIntervalMs) {
    // Fixed-point points (100 units = 1 point) keep ties reproducible. All
    // business bonuses are bounded; accumulated waiting grows without a cap.
    // Concave waiting growth avoids reducing a busy shop to arrival order.
    // Correct the floating-point sqrt estimate using exact integer squares so
    // score boundaries and ties remain identical across platforms.
    const auto ageUnits = waited * 100 / agingIntervalMs;
    auto root = static_cast<TimeMs>(std::sqrt(static_cast<double>(ageUnits)));
    while ((root + 1) * (root + 1) <= ageUnits) ++root;
    while (root * root > ageUnits) --root;
    const auto aging = root * 10;
    const int service = job.service == ServiceTier::Express ? 3200 :
        job.service == ServiceTier::Standard ? 1600 : 0;
    const auto window = serviceWindowMs(job.service);
    const auto remaining = std::max<TimeMs>(0, job.deadlineMs - now);
    const auto urgency = job.deadlineMs == 0 || remaining == 0 ? 0 :
        400 * (window - std::min(window, remaining)) / window;
    // Queue ranking uses nominal consumables, independent of idle devices.
    // Actual device cost is computed and recorded only after assignment.
    const auto contribution = std::max<Money>(0, job.quotedCents - directCost(job, Printer{}));
    const auto earnings = std::min<Money>(3200, 3200 * contribution / (100 * job.sheets()));
    return 100 + aging + service + urgency + earnings;
}
bool precedes(const QueueKey& a, const QueueKey& b, Policy policy) {
    if (policy == Policy::ShortestJobFirst && a.sheets != b.sheets) return a.sheets < b.sheets;
    if (policy == Policy::Balanced) {
        // Beyond the longest offered service window, protect old documents
        // ahead of new business bonuses. This is queue priority, not a deadline
        // guarantee when devices or paper are unavailable.
        const auto protection = serviceWindowMs(ServiceTier::Flexible);
        const bool oldA = a.waitedMs >= protection, oldB = b.waitedMs >= protection;
        if (oldA != oldB) return oldA;
        if (oldA && a.waitedMs != b.waitedMs) return a.waitedMs > b.waitedMs;
        // Weighted response ratio: useful work per face, with integer cross
        // products for reproducible comparisons and no rounded ratio ties.
        const auto lhs = a.score * b.sheets;
        const auto rhs = b.score * a.sheets;
        if (lhs != rhs) return lhs > rhs;
    }
    return a.queuedSinceMs < b.queuedSinceMs;
}
// Arrived Queued jobs accepted by `eligible`, keyed for `policy`, in list order.
template <class Eligible>
std::vector<QueueKey> waitingKeys(const std::vector<Job>& jobs, Policy policy, TimeMs now,
                                  TimeMs agingIntervalMs, Eligible eligible) {
    std::vector<QueueKey> keys;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const auto& job = jobs[i];
        if (job.state != JobState::Queued || job.queuedSinceMs > now || !eligible(job)) continue;
        const auto waited = job.accumulatedWaitMs + now - job.queuedSinceMs;
        keys.push_back({i, job.sheets(), job.queuedSinceMs, waited,
                        policy == Policy::Balanced ? balancedScore(job, waited, now, agingIntervalMs) : 0});
    }
    return keys;
}
void sortKeys(std::vector<QueueKey>& keys, Policy policy) {
    std::stable_sort(keys.begin(), keys.end(), [policy](const QueueKey& a, const QueueKey& b) {
        return precedes(a, b, policy);
    });
}
}

Scheduler::Scheduler(SchedulerOptions options, Clock clock) : options_(options), clock_(std::move(clock)) {
    if (options_.agingIntervalMs < 1 || options_.agingIntervalMs > MaxTimeMs)
        throw std::invalid_argument("aging interval must be positive milliseconds");
    if (!clock_) {
        const auto epoch = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto anchor = std::chrono::steady_clock::now();
        clock_ = [epoch, anchor] {
            return epoch + std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - anchor).count();
        };
    }
}
TimeMs Scheduler::nowMs() const { return clock_(); }
bool Scheduler::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) {
            return job.state == JobState::Running || job.state == JobState::Cancelling;
        })) return false;
    jobs_.clear();
    return true;
}
Job* Scheduler::find(const std::string& id) {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    return it == jobs_.end() ? nullptr : &*it;
}
bool Scheduler::submit(Job job) {
    if (!validJob(job) || job.state != JobState::Queued || job.attempts != 0 || job.costCents != 0 || job.assignedPrinter != -1 ||
        !job.runs.empty() || job.firstStartedMs || job.finishedMs || job.accumulatedWaitMs != 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (find(job.id)) return false;
    job.queuedSinceMs = job.submittedMs;
    job.lastChangedMs = job.submittedMs;
    jobs_.push_back(std::move(job));
    return true;
}
bool Scheduler::submitBatch(std::vector<Job> jobs) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> ids;
    for (const auto& existing : jobs_) ids.insert(existing.id);
    for (const auto& job : jobs) {
        if (!validJob(job) || job.state != JobState::Queued || job.attempts != 0 || job.costCents != 0 || job.assignedPrinter != -1 ||
            !job.runs.empty() || job.firstStartedMs || job.finishedMs || job.accumulatedWaitMs != 0 ||
            !ids.insert(job.id).second) return false;
    }
    for (auto& job : jobs) {
        job.queuedSinceMs = job.submittedMs;
        job.lastChangedMs = job.submittedMs;
        jobs_.push_back(std::move(job));
    }
    return true;
}
std::vector<std::string> Scheduler::waitingOrder(Policy policy, TimeMs now) const {
    if (!validTime(now) || !validPolicy(policy)) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    auto keys = waitingKeys(jobs_, policy, now, options_.agingIntervalMs, [](const Job&) { return true; });
    sortKeys(keys, policy);
    std::vector<std::string> ids;
    for (const auto& key : keys) ids.push_back(jobs_[key.index].id);
    return ids;
}
std::optional<Job> Scheduler::claim(Policy policy) { return claim(policy, nowMs()); }
std::optional<Job> Scheduler::claim(Policy policy, TimeMs now) {
    if (!validTime(now) || !validPolicy(policy)) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto keys = waitingKeys(jobs_, policy, now, options_.agingIntervalMs, [](const Job&) { return true; });
    if (keys.empty()) return std::nullopt;
    // The first minimum keeps list order among equal keys, like the stable sort.
    const auto best = std::min_element(keys.begin(), keys.end(), [policy](const QueueKey& a, const QueueKey& b) {
        return precedes(a, b, policy);
    });
    return startLocked(jobs_[best->index], now);
}
Job Scheduler::startLocked(Job& value, TimeMs now) {
    auto* best = &value;
    best->accumulatedWaitMs += now - best->queuedSinceMs;
    best->runs.push_back({now, std::nullopt});
    ++best->attempts;
    if (!best->firstStartedMs) best->firstStartedMs = now;
    best->state = JobState::Running;
    best->lastChangedMs = now;
    return *best;
}
std::optional<Job> Scheduler::claimFor(Policy policy, TimeMs now, const std::vector<Printer>& fleet,
                                      const std::vector<int>& idle, Inventory supplied) {
    if (!validTime(now) || !validPolicy(policy) || fleet.empty() || fleet.size() > 64 ||
        supplied.a4 < 0 || supplied.a3 < 0) return std::nullopt;
    for (const auto& p : fleet) if (!validPrinter(p)) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto stock = availablePaper(jobs_, supplied);
    std::vector<bool> occupied(fleet.size(), false);
    for (const auto& other : jobs_) if (other.assignedPrinter >= 0 && other.assignedPrinter < static_cast<int>(fleet.size()) &&
        (other.state == JobState::Running || other.state == JobState::Cancelling)) occupied[other.assignedPrinter] = true;
    // Layer 1: exactly the same queue comparison as waitingOrder() / claim().
    auto keys = waitingKeys(jobs_, policy, now, options_.agingIntervalMs, [&](const Job& j) {
        return j.attempts > 0 || j.paperSheets() <= (j.paper == PaperSize::A3 ? stock.a3 : stock.a4);
    });
    sortKeys(keys, policy);
    std::vector<Job*> waiting;
    for (const auto& key : keys) waiting.push_back(&jobs_[key.index]);
    // Layer 2: normalize idle indices, then choose a device for the queue head.
    std::vector<int> free;
    for (int i : idle)
        if (i >= 0 && i < static_cast<int>(fleet.size()) && !occupied[i] && fleet[i].online) free.push_back(i);
    std::sort(free.begin(), free.end());
    free.erase(std::unique(free.begin(), free.end()), free.end());
    const auto usable = [&](const Job& j, int i) {
        return compatible(j, fleet[i]) && processingMs(j, fleet[i]) <= MaxTimeMs - now;
    };
    std::vector<int> exclusiveDemand(fleet.size(), 0);
    if (policy == Policy::Balanced) {
        for (const auto* j : waiting) {
            int only = -1, count = 0;
            for (int i : free) if (usable(*j, i)) { only = i; ++count; }
            if (count == 1) ++exclusiveDemand[only];
        }
    }
    for (auto* j : waiting) {
        int device = -1;
        const auto rank = [&](int i) {
            const auto& p = fleet[i];
            const int surplus = int(p.a3 && j->paper != PaperSize::A3) +
                int(p.color && !j->color) + int(p.duplex && !j->duplex);
            return std::make_tuple(policy == Policy::Balanced ? exclusiveDemand[i] : 0,
                                  processingMs(*j, p), directCost(*j, p),
                                  policy == Policy::Balanced ? surplus : 0, i);
        };
        for (int i : free) {
            if (!usable(*j, i)) continue;
            if (device < 0 || rank(i) < rank(device)) device = i;
            if (policy == Policy::Fcfs) break;
        }
        // Blocked documents keep their place, but do not idle unrelated devices.
        if (device < 0) continue;
        j->assignedPrinter = device;
        j->costCents = directCost(*j, fleet[device]);
        return startLocked(*j, now);
    }
    return std::nullopt;
}
bool Scheduler::refund(const std::string& id, Money cumulativeCents) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* job = find(id);
    if (!job || job->state != JobState::Completed || cumulativeCents < job->refundCents ||
        cumulativeCents > job->quotedCents) return false;
    job->refundCents = cumulativeCents;
    return true;
}
bool Scheduler::finishLocked(Job& job, int attempt, bool success, TimeMs now) {
    if ((job.state != JobState::Running && job.state != JobState::Cancelling) ||
        !active(job, attempt, now)) return false;
    // Confirmed atomic output wins if completion races with cancellation.
    if (job.state == JobState::Cancelling && !success) return false;
    job.runs.back().stoppedMs = now;
    job.finishedMs = now;
    job.lastChangedMs = now;
    job.state = success ? JobState::Completed : JobState::Failed;
    return true;
}
bool Scheduler::finish(const std::string& id, bool success) {
    const auto now = nowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    auto* job = find(id);
    return job && job->attempts == 1 && finishLocked(*job, 1, success, now);
}
bool Scheduler::finish(const std::string& id, int attempt, bool success, TimeMs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* job = find(id);
    return job && finishLocked(*job, attempt, success, now);
}
bool Scheduler::cancel(const std::string& id, TimeMs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* job = find(id);
    if (!job || !validTime(now) || now < job->lastChangedMs) return false;
    if (job->state == JobState::Queued) {
        job->accumulatedWaitMs += now - job->queuedSinceMs;
        job->state = JobState::Cancelled;
        job->finishedMs = now;
    } else if (job->state == JobState::Running) {
        job->state = JobState::Cancelling;
    } else {
        return false;
    }
    job->lastChangedMs = now;
    return true;
}
bool Scheduler::acknowledgeCancel(const std::string& id, int attempt, TimeMs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* job = find(id);
    if (!job || job->state != JobState::Cancelling || !active(*job, attempt, now)) return false;
    job->runs.back().stoppedMs = now;
    job->state = JobState::Cancelled;
    job->finishedMs = now;
    job->lastChangedMs = now;
    return true;
}
bool Scheduler::retry(const std::string& id, int attempt, TimeMs now) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* job = find(id);
    if (!job || job->state != JobState::Running || !active(*job, attempt, now)) return false;
    job->runs.back().stoppedMs = now;
    job->lastChangedMs = now;
    if (job->attempts == job->maxAttempts) {
        job->state = JobState::Failed;
        job->finishedMs = now;
    } else {
        job->state = JobState::Queued;
        job->queuedSinceMs = now;
    }
    return true;
}
bool Scheduler::restore(const std::vector<Job>& jobs, TimeMs now) {
    if (!validTime(now)) return false;
    std::set<std::string> ids;
    for (const auto& job : jobs) {
        if (!validSnapshot(job, now) || !ids.insert(job.id).second) return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!jobs_.empty()) return false;
    jobs_ = jobs;
    return true;
}
std::vector<Job> Scheduler::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_;
}
} // namespace cps
