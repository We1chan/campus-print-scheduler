#include "core_test_support.hpp"
#include "cps/core/shop.hpp"
#include <atomic>
#include <set>
#include <thread>

using namespace cps;
using test::require;
using test::claim;
using test::job;
namespace {
void agingBoundaries() {
    for (const auto time : {102399LL, 102400LL, 320000LL}) {
        Scheduler s({100});
        auto old = job("old", 10), urgent = job("urgent", 10, time);
        old.service = ServiceTier::Flexible; urgent.service = ServiceTier::Express;
        require(s.submit(old) && s.submit(urgent), "flexible and express jobs submitted");
        require(claim(s, Policy::Balanced, time).id == (time == 102399 ? "urgent" : "old"),
                "uncapped aging overcomes service bonus and ties use FIFO");
    }
    Scheduler tied({100});
    tied.submit(job("first", 10, 0, 0)); tied.submit(job("second", 10, 0, 100));
    require(claim(tied, Policy::Balanced, 0).id == "first", "legacy priority cannot override stable score ties");
    Scheduler pressure({100});
    auto old = job("old-large", 1000); old.service = ServiceTier::Flexible; quote(old);
    const auto protection = serviceWindowMs(ServiceTier::Flexible);
    auto fresh = job("fresh-express", 1, protection); fresh.service = ServiceTier::Express;
    fresh.color = true; quote(fresh); fresh.deadlineMs = fresh.submittedMs;
    require(pressure.submit(old) && pressure.submit(fresh), "worst-case bounded business bonuses");
    require(claim(pressure, Policy::Balanced, protection).id == old.id,
            "long-wait protection lets an old large flexible document beat a new premium short document");
}
void nonPreemptionAndArrival() {
    Scheduler s;
    s.submit(job("future", 1, 100, 100)); s.submit(job("running", 20, 0));
    require(claim(s, Policy::ShortestJobFirst, 0).id == "running", "future job must not start early");
    require(!s.claim(Policy::Balanced, 99), "policy change cannot reclaim running job");
    require(claim(s, Policy::Balanced, 100).id == "future", "arrival becomes eligible");
    require(s.snapshot()[1].state == JobState::Running, "running job is not preempted");
}
void retryWaitingAndTokens() {
    Scheduler s({100});
    s.submit(job("retry", 1)); s.submit(job("other", 1, 200));
    auto first = claim(s, Policy::Fcfs, 100);
    require(s.retry(first.id, first.attempts, 200), "retry closes first run");
    require(!s.retry(first.id, first.attempts, 200), "duplicate retry rejected");
    auto next = claim(s, Policy::Balanced, 200);
    require(next.id == "retry" && next.attempts == 2, "past queue waiting survives retry");
    require(next.accumulatedWaitMs == 100, "execution time does not become queue waiting");
    require(!s.finish(next.id, 1, true, 250), "stale attempt must not finish a new attempt");
    require(!s.finish(next.id, true), "legacy finish must not resolve reclaims");
    require(s.finish(next.id, 2, true, 250), "current token completes");
    require(!s.finish(next.id, 2, false, 260), "terminal state immutable");
    const auto result = s.snapshot()[0];
    require(result.firstStartedMs == 100 && result.finishedMs == 250 && result.runs.size() == 2,
            "first start and complete attempt history retained");
    Scheduler fifo;
    fifo.submit(job("retry")); claim(fifo, Policy::Fcfs, 0);
    fifo.submit(job("waiting", 1, 5)); require(fifo.retry("retry", 1, 10), "requeue");
    require(claim(fifo, Policy::Fcfs, 10).id == "waiting", "retry enters queue behind existing jobs");
}
void limitAndClock() {
    TimeMs now = 100;
    Scheduler s({}, [&now] { return now; });
    auto j = job("one", 1, now); j.maxAttempts = 1;
    require(s.submit(j), "maxAttempts 1 allowed");
    require(s.claim(Policy::Fcfs)->firstStartedMs == 100, "injected clock used by claim");
    require(!s.retry("one", 1, 99) && !s.cancel("one", 99), "backwards mutation refused");
    now = 110;
    require(s.retry("one", 1, now), "last failed attempt recorded");
    require(s.snapshot()[0].state == JobState::Failed && !s.claim(Policy::Fcfs), "limit terminates job");
    Scheduler end;
    end.submit(job("edge", 1, MaxTimeMs));
    require(claim(end, Policy::Balanced, MaxTimeMs).id == "edge", "maximum exact timestamp accepted");
    require(!end.finish("edge", 1, true, MaxTimeMs + 1), "out-of-range mutation rejected");
    require(end.finish("edge", 1, true, MaxTimeMs), "zero duration boundary permitted");
    Scheduler oldest({1}); oldest.submit(job("oldest", 1000));
    require(claim(oldest, Policy::Balanced, MaxTimeMs).id == "oldest", "maximum cumulative wait does not overflow score");
}
void cancelStates() {
    Scheduler s;
    s.submit(job("waiting")); require(s.cancel("waiting", 10), "queued cancel immediate");
    require(!s.claim(Policy::Fcfs, 10), "cancelled job cannot be claimed");
    require(!s.cancel("waiting", 11) && !s.finish("waiting", 1, true, 11), "cancel is terminal");
    s.submit(job("running", 1, 20)); claim(s, Policy::Fcfs, 20);
    require(s.cancel("running", 30), "running cancel expresses intent");
    require(s.snapshot()[1].state == JobState::Cancelling && !s.snapshot()[1].finishedMs,
            "no fabricated stop time before executor confirmation");
    require(!s.retry("running", 1, 31) && !s.finish("running", 1, false, 31), "cancel cannot become retry/failure");
    require(!s.acknowledgeCancel("running", 2, 31), "wrong token cannot acknowledge");
    require(s.acknowledgeCancel("running", 1, 40), "correct stop confirmation");
    require(s.snapshot()[1].finishedMs == 40 && s.snapshot()[1].runs[0].stoppedMs == 40, "stop time recorded");
}
void completionRace() {
    for (int round = 0; round < 40; ++round) {
        Scheduler s; s.submit(job("race")); claim(s, Policy::Fcfs, 0);
        test::Gate gate(2); bool finished = false;
        std::thread cancel([&] { gate.arrive(); s.cancel("race", 10); });
        std::thread complete([&] { gate.arrive(); finished = s.finish("race", 1, true, 10); });
        cancel.join(); complete.join();
        require(finished && s.snapshot()[0].state == JobState::Completed, "confirmed completion wins cancel intent");
    }
}
void claimCancelRace() {
    for (int round = 0; round < 40; ++round) {
        Scheduler s; s.submit(job("race")); test::Gate gate(2); std::optional<Job> result;
        std::thread cancel([&] { gate.arrive(); s.cancel("race", 10); });
        std::thread dispatch([&] { gate.arrive(); result = s.claim(Policy::Fcfs, 10); });
        cancel.join(); dispatch.join();
        const auto state = s.snapshot()[0].state;
        require(result ? state == JobState::Cancelling : state == JobState::Cancelled,
                "claim and cancellation must have a consistent winner");
    }
}
void concurrentIdentity() {
    Scheduler s; test::Gate gate(8); std::atomic<int> producersLeft{4}, failures{0};
    std::mutex idsMutex; std::vector<std::string> ids; std::vector<std::thread> threads;
    for (int p = 0; p < 4; ++p) threads.emplace_back([&, p] {
        gate.arrive();
        for (int i = 0; i < 100; ++i) if (!s.submit(job(std::to_string(p * 100 + i)))) ++failures;
        --producersLeft;
    });
    for (int c = 0; c < 4; ++c) threads.emplace_back([&] {
        gate.arrive();
        for (;;) {
            auto next = s.claim(Policy::Balanced, 1000);
            if (!next && producersLeft.load() == 0) {
                next = s.claim(Policy::Balanced, 1000); // observe submissions before producer completion
                if (!next) break;
            }
            if (!next) { std::this_thread::yield(); continue; }
            { std::lock_guard<std::mutex> lock(idsMutex); ids.push_back(next->id); }
            if (!s.finish(next->id, next->attempts, true, 1001)) ++failures;
        }
    });
    for (auto& thread : threads) thread.join();
    require(failures == 0 && ids.size() == 400, "all submissions/finishes succeed under contention");
    const std::set<std::string> unique(ids.begin(), ids.end());
    require(unique.size() == 400, "check claimed identities, not only finish counts");
    for (int i = 0; i < 400; ++i) require(unique.count(std::to_string(i)) == 1, "no job lost or duplicated");
}
std::vector<Job> allStates() {
    std::vector<Job> result;
    for (int mode = 0; mode < 7; ++mode) {
        Scheduler s; s.submit(job(std::to_string(mode)));
        if (mode == 4) s.cancel(std::to_string(mode), 20);
        else if (mode != 0) {
            claim(s, Policy::Fcfs, 10);
            if (mode == 2) s.finish(std::to_string(mode), 1, true, 20);
            if (mode == 3) s.finish(std::to_string(mode), 1, false, 20);
            if (mode == 5) s.cancel(std::to_string(mode), 20);
            if (mode == 6) s.retry(std::to_string(mode), 1, 20);
        }
        result.push_back(s.snapshot()[0]);
    }
    return result;
}
void restoreAndIsolation() {
    Scheduler s; auto rows = allStates(); require(s.restore(rows, 20), "restore all valid states");
    rows[0].id = "changed";
    require(s.snapshot()[0].id == "0", "restore owns a copy");
    auto copy = s.snapshot(); copy[0].priority = 99;
    require(s.snapshot()[0].priority == 0, "snapshot owns a copy");
    require(!s.restore({}, 20), "cannot replace a nonempty scheduler");
    require(s.retry("1", 1, 30), "recovered running job can be reconciled for retry");
    require(s.acknowledgeCancel("5", 1, 30), "recovered cancelling job can be acknowledged");
    require(s.snapshot()[2].state == JobState::Completed, "completed task stays completed");
}
void malformedSnapshots() {
    const auto original = allStates();
    const std::vector<std::function<void(std::vector<Job>&)>> mutations = {
        [](auto& r) { r[1].id = r[0].id; },
        [](auto& r) { r[1].attempts = 2; },
        [](auto& r) { r[1].runs[0].startedMs = -1; },
        [](auto& r) { r[2].runs[0].stoppedMs = 9; },
        [](auto& r) { r[2].finishedMs = 21; },
        [](auto& r) { r[2].accumulatedWaitMs = 0; },
        [](auto& r) { r[1].firstStartedMs = 0; },
        [](auto& r) { r[1].queuedSinceMs = -1; },
        [](auto& r) { r[1].queuedSinceMs = 5; },
        [](auto& r) { r[1].lastChangedMs = 11; },
        [](auto& r) { r[6].attempts = r[6].maxAttempts; },
        [](auto& r) { r[0].state = static_cast<JobState>(100); }
    };
    for (std::size_t i = 0; i < mutations.size(); ++i) {
        auto rows = original; mutations[i](rows); Scheduler s;
        require(!s.restore(rows, 20), "malformed snapshot rejected: case " + std::to_string(i));
        require(s.snapshot().empty(), "failed restore is atomic");
    }
}
void batchAndPreview() {
    Scheduler atomic;
    require(atomic.submit(job("existing")), "existing job");
    require(!atomic.submitBatch({job("new"), job("existing")}), "reject whole duplicate batch");
    require(atomic.snapshot().size() == 1, "no partial batch on duplicate");
    auto bad = job("bad"); bad.pages = 0;
    require(!atomic.submitBatch({job("new"), bad}), "reject whole invalid batch");
    require(atomic.snapshot().size() == 1, "no partial batch on invalid fields");
    for (auto policy : {Policy::Fcfs, Policy::ShortestJobFirst, Policy::Balanced}) {
        Scheduler s({10});
        auto urgent = job("urgent", 3, 10, 4); urgent.service = ServiceTier::Express;
        require(s.submitBatch({job("old", 20, 0), job("short", 1, 10),
                              urgent, job("future", 1, 100)}), "batch accepted");
        const auto before = s.snapshot();
        const auto preview = s.waitingOrder(policy, 50);
        const std::vector<std::string> expected = policy == Policy::Fcfs ?
            std::vector<std::string>{"old", "short", "urgent"} : policy == Policy::ShortestJobFirst ?
            std::vector<std::string>{"short", "urgent", "old"} : std::vector<std::string>{"short", "urgent", "old"};
        require(preview == expected, "preview respects configured aging, policy and future arrivals");
        require(s.snapshot()[0].attempts == before[0].attempts, "preview does not mutate state");
        for (const auto& id : preview) require(claim(s, policy, 50).id == id, "preview equals actual claims");
    }
}
void invalidInputs() {
    Scheduler s;
    for (const int p : {-1, 101}) { auto j=job("bad"); j.priority=p; require(!s.submit(j), "priority bounds"); }
    for (const int n : {0, 101}) { auto j=job("bad"); j.maxAttempts=n; require(!s.submit(j), "attempt bounds"); }
    auto j=job("bad"); j.firstStartedMs=0; require(!s.submit(j), "fresh submit rejects history");
    require(!s.claim(static_cast<Policy>(99), 0), "invalid policy rejected");
    test::throws<std::invalid_argument>([] { Scheduler invalid({0}); }, "zero aging interval rejected");
}
}
int main() {
    return test::run({{"aging thresholds and tie rules", agingBoundaries},
        {"future arrivals and non-preemption", nonPreemptionAndArrival},
        {"retry waiting and stale tokens", retryWaitingAndTokens}, {"attempt limits and clock", limitAndClock},
        {"cancel state transitions", cancelStates}, {"completion versus cancel race", completionRace},
        {"claim versus cancel race", claimCancelRace}, {"concurrent producers and unique claims", concurrentIdentity},
        {"restore and snapshot isolation", restoreAndIsolation}, {"malformed snapshot atomicity", malformedSnapshots},
        {"batch atomicity and authoritative queue preview", batchAndPreview}, {"input validation", invalidInputs}});
}
