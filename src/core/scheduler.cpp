#include "cps/core/scheduler.hpp"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>
#include <utility>

namespace cps {
namespace {
bool validTime(TimeMs time) { return time >= 0 && time <= MaxTimeMs; }
bool validJob(const Job& job) {
    return !job.id.empty() && !job.title.empty() && job.pages >= 1 && job.pages <= 1000 &&
        job.copies >= 1 && job.copies <= 100 && job.pages * job.copies <= 10000 &&
        validTime(job.submittedMs) && job.priority >= 0 && job.priority <= 100 &&
        job.maxAttempts >= 1 && job.maxAttempts <= 100;
}
bool active(const Job& job, int attempt, TimeMs now) {
    return validTime(now) && now >= job.lastChangedMs && attempt > 0 &&
        attempt == job.attempts && !job.runs.empty() && !job.runs.back().stoppedMs;
}
bool validSnapshot(const Job& job, TimeMs now) {
    if (!validJob(job) || job.submittedMs > now || job.lastChangedMs < job.submittedMs ||
        job.lastChangedMs > now || job.attempts < 0 || job.attempts > job.maxAttempts ||
        static_cast<std::size_t>(job.attempts) != job.runs.size()) return false;
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
Job* Scheduler::find(const std::string& id) {
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.id == id; });
    return it == jobs_.end() ? nullptr : &*it;
}
bool Scheduler::submit(Job job) {
    if (!validJob(job) || job.state != JobState::Queued || job.attempts != 0 ||
        !job.runs.empty() || job.firstStartedMs || job.finishedMs || job.accumulatedWaitMs != 0)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (find(job.id)) return false;
    job.queuedSinceMs = job.submittedMs;
    job.lastChangedMs = job.submittedMs;
    jobs_.push_back(std::move(job));
    return true;
}
std::optional<Job> Scheduler::claim(Policy policy) { return claim(policy, nowMs()); }
std::optional<Job> Scheduler::claim(Policy policy, TimeMs now) {
    if (!validTime(now) || (policy != Policy::Fcfs && policy != Policy::ShortestJobFirst &&
                          policy != Policy::PriorityAging)) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    Job* best = nullptr;
    const auto score = [&](const Job& job) {
        return job.priority + (job.accumulatedWaitMs + now - job.queuedSinceMs) / options_.agingIntervalMs;
    };
    for (auto& job : jobs_) {
        if (job.state != JobState::Queued || job.queuedSinceMs > now) continue;
        bool better = best == nullptr;
        if (best) {
            bool tied = true;
            if (policy == Policy::ShortestJobFirst) {
                better = job.sheets() < best->sheets();
                tied = job.sheets() == best->sheets();
            } else if (policy == Policy::PriorityAging) {
                better = score(job) > score(*best);
                tied = score(job) == score(*best);
            }
            if (tied) better = job.queuedSinceMs < best->queuedSinceMs;
        }
        if (better) best = &job;
    }
    if (!best) return std::nullopt;
    best->accumulatedWaitMs += now - best->queuedSinceMs;
    best->runs.push_back({now, std::nullopt});
    ++best->attempts;
    if (!best->firstStartedMs) best->firstStartedMs = now;
    best->state = JobState::Running;
    best->lastChangedMs = now;
    return *best;
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
