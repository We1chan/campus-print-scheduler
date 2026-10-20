#include "cps/core/scheduler.hpp"

#include <atomic>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
cps::Job job(const std::string& id, int pages, std::int64_t submitted, int copies = 1) {
    return {id, "demo", pages, copies, submitted, cps::JobState::Queued};
}
}

int main() {
    try {
        cps::Scheduler fifo;
        require(!fifo.claim(cps::Policy::Fcfs), "empty queue must not return a job");
        require(!fifo.submit(job("bad", 0, 0)), "reject zero pages");
        require(!fifo.submit(job("overflow", 1000, 0, 100)), "bound total sheets");
        require(fifo.submit(job("later", 1, 20)), "submit later job");
        require(fifo.submit(job("first", 10, 10)), "submit earlier job");
        require(!fifo.submit(job("first", 3, 30)), "duplicate ID rejected");
        require(!fifo.finish("first", true), "queued job cannot complete");
        require(fifo.claim(cps::Policy::Fcfs)->id == "first", "FCFS uses submission time");
        require(fifo.claim(cps::Policy::Fcfs)->id == "later", "claim reserves job exactly once");
        require(fifo.finish("first", true), "running job completes");
        require(!fifo.finish("first", false), "terminal states cannot be changed");
        require(!fifo.clear(), "clear cannot discard a running execution");
        require(fifo.cancel("later", fifo.nowMs()), "cancel the remaining running execution");
        require(!fifo.clear(), "clear must wait for cancellation acknowledgement");
        require(fifo.acknowledgeCancel("later", 1, fifo.nowMs()), "executor confirms cancellation");
        require(fifo.clear() && fifo.snapshot().empty(), "clear atomically removes all inactive orders");
        require(fifo.submit(job("next-demo", 4, 0)) && fifo.clear(), "clear also removes waiting orders");

        cps::Scheduler shortest;
        shortest.submit(job("copies", 2, 0, 5));
        shortest.submit(job("short-1", 3, 1));
        shortest.submit(job("short-2", 3, 1));
        require(shortest.claim(cps::Policy::ShortestJobFirst)->id == "short-1", "SJF uses total sheets");
        require(shortest.claim(cps::Policy::ShortestJobFirst)->id == "short-2", "stable tie ordering");
        require(shortest.claim(cps::Policy::ShortestJobFirst)->id == "copies", "long job eventually claimed");

        cps::Scheduler concurrent;
        std::vector<std::thread> producers;
        std::atomic<int> submitted{0};
        for (int producer = 0; producer < 4; ++producer) {
            producers.emplace_back([&, producer] {
                for (int index = 0; index < 100; ++index) {
                    if (concurrent.submit(job(std::to_string(producer * 100 + index), 1, index))) ++submitted;
                }
            });
        }
        for (auto& producer : producers) producer.join();
        std::atomic<int> completed{0};
        std::vector<std::thread> consumers;
        for (int index = 0; index < 4; ++index) {
            consumers.emplace_back([&] {
                while (const auto next = concurrent.claim(cps::Policy::Fcfs)) {
                    if (concurrent.finish(next->id, true)) ++completed;
                }
            });
        }
        for (auto& consumer : consumers) consumer.join();
        require(submitted == 400 && completed == 400, "concurrent jobs must not be lost or duplicated");
        for (const auto& item : concurrent.snapshot()) {
            require(item.state == cps::JobState::Completed, "all concurrent jobs completed");
        }
        std::cout << "Scheduler ordering, validation, state transitions and concurrency passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
