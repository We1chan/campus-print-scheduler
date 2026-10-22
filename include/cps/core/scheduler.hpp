#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cps {
using TimeMs = std::int64_t;
constexpr TimeMs MaxTimeMs = 9007199254740991LL; // exactly representable in JSON doubles
// Each policy defines queue ordering first, then compatible-printer assignment.
enum class Policy { Fcfs, ShortestJobFirst, Balanced };
enum class PaperSize { A4, A3 };
enum class ServiceTier { Express, Standard, Flexible };
// Counter: settled at the shop. The others are simulated online prepayments.
enum class PaymentMethod { Counter, Wechat, Alipay, CampusCard };
using Money = std::int64_t; // integer cents, never floating point currency
struct Printer {
    std::string name;
    bool a3 = false;
    bool color = false;
    bool duplex = true;
    bool online = true;
    TimeMs monoPageMs = 1500;
    TimeMs colorPageMs = 3000;
};
struct Inventory { int a4 = 10000; int a3 = 3000; };
enum class JobState { Queued, Running, Completed, Failed, Cancelling, Cancelled };
struct RunInterval {
    TimeMs startedMs = 0;
    std::optional<TimeMs> stoppedMs = std::nullopt;
};
struct Job {
    std::string id;
    std::string title;
    int pages = 1;
    int copies = 1;
    TimeMs submittedMs = 0;
    JobState state = JobState::Queued;
    int priority = 0;                 // 0..100; legacy metadata, no longer a scheduling input
    int maxAttempts = 3;              // 1..100, including the first attempt
    int attempts = 0;                 // token for the most recent claim
    TimeMs queuedSinceMs = 0;
    TimeMs accumulatedWaitMs = 0;
    TimeMs lastChangedMs = 0;
    std::optional<TimeMs> firstStartedMs = std::nullopt;
    std::optional<TimeMs> finishedMs = std::nullopt;
    std::vector<RunInterval> runs = {};
    PaperSize paper = PaperSize::A4;
    bool color = false;
    bool duplex = false;
    ServiceTier service = ServiceTier::Standard;
    TimeMs deadlineMs = 0; // 0 means legacy order without a delivery promise
    Money baseCents = 0;
    Money quotedCents = 0;
    Money costCents = 0;
    Money refundCents = 0;
    int assignedPrinter = -1;
    PaymentMethod payment = PaymentMethod::Counter;
    std::string customer = {};        // self-service account, empty for counter orders
    int sheets() const { return pages * copies; }
    int paperSheets() const { return ((pages + (duplex ? 1 : 0)) / (duplex ? 2 : 1)) * copies; }
};
struct SchedulerOptions { TimeMs agingIntervalMs = 30000; };

// All mutations and snapshots are mutex-protected; no I/O under this lock.
// Explicit times and injected clocks must share submittedMs's timeline.
class Scheduler {
public:
    using Clock = std::function<TimeMs()>;
    explicit Scheduler(SchedulerOptions options = {}, Clock clock = {});
    TimeMs nowMs() const;
    bool submit(Job job);
    bool submitBatch(std::vector<Job> jobs);
    std::vector<std::string> waitingOrder(Policy policy, TimeMs now) const;
    std::optional<Job> claim(Policy policy);
    std::optional<Job> claim(Policy policy, TimeMs now);
    // Order the queue, then assign the first dispatchable document to a printer.
    // Both layers, device ownership and paper reservation share the queue mutex.
    std::optional<Job> claimFor(Policy policy, TimeMs now, const std::vector<Printer>& fleet,
                               const std::vector<int>& idle, Inventory supplied);
    bool refund(const std::string& id, Money cumulativeCents);
    // Compatibility entry point for the original, single-claim GUI.
    bool finish(const std::string& id, bool success);
    bool finish(const std::string& id, int attempt, bool success, TimeMs now);
    bool cancel(const std::string& id, TimeMs now);
    // Call only after the executor has confirmed this attempt stopped.
    bool acknowledgeCancel(const std::string& id, int attempt, TimeMs now);
    bool retry(const std::string& id, int attempt, TimeMs now);
    // Load a validated snapshot into an empty scheduler. B reconciles outputs
    // and processes before calling finish/retry/acknowledgeCancel for recovery.
    bool restore(const std::vector<Job>& jobs, TimeMs now);
    // The owner must stop executors before discarding their order history.
    bool clear();
    std::vector<Job> snapshot() const;
private:
    Job* find(const std::string& id);
    bool finishLocked(Job& job, int attempt, bool success, TimeMs now);
    Job startLocked(Job& job, TimeMs now);
    SchedulerOptions options_;
    Clock clock_;
    mutable std::mutex mutex_;
    std::vector<Job> jobs_;
};
} // namespace cps
