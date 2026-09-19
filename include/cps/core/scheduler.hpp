#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cps {
enum class Policy { Fcfs, ShortestJobFirst };
enum class JobState { Queued, Running, Completed, Failed };

struct Job {
    std::string id;
    std::string title;
    int pages = 1;
    int copies = 1;
    std::int64_t submittedMs = 0;
    JobState state = JobState::Queued;
    int sheets() const { return pages * copies; }
};

// One owner per claimed job. Methods may be called from different threads.
// Policy changes affect queued jobs only; printing is non-preemptive.
class Scheduler {
public:
    bool submit(Job job);
    std::optional<Job> claim(Policy policy);
    bool finish(const std::string& id, bool success);
    std::vector<Job> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<Job> jobs_;
};
} // namespace cps
