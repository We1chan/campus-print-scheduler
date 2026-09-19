#include "cps/core/scheduler.hpp"

#include <algorithm>
#include <utility>

namespace cps {
bool Scheduler::submit(Job job) {
    if (job.id.empty() || job.title.empty() || job.pages < 1 || job.pages > 1000 ||
        job.copies < 1 || job.copies > 100 || job.pages * job.copies > 10000 ||
        job.submittedMs < 0 || job.state != JobState::Queued) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::any_of(jobs_.begin(), jobs_.end(), [&](const Job& other) {
            return other.id == job.id;
        })) {
        return false;
    }
    jobs_.push_back(std::move(job));
    return true;
}

std::optional<Job> Scheduler::claim(Policy policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    Job* best = nullptr;
    for (auto& job : jobs_) {
        if (job.state != JobState::Queued) continue;
        if (best == nullptr ||
            (policy == Policy::ShortestJobFirst && job.sheets() < best->sheets()) ||
            ((policy == Policy::Fcfs || job.sheets() == best->sheets()) &&
             job.submittedMs < best->submittedMs)) {
            best = &job;
        }
    }
    if (best == nullptr) return std::nullopt;
    best->state = JobState::Running;
    return *best;
}

bool Scheduler::finish(const std::string& id, bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& job : jobs_) {
        if (job.id == id && job.state == JobState::Running) {
            job.state = success ? JobState::Completed : JobState::Failed;
            return true;
        }
    }
    return false;
}

std::vector<Job> Scheduler::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_;
}
} // namespace cps
