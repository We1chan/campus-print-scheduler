#pragma once
#include "cps/core/scheduler.hpp"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace test {
inline void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
inline void near(double actual, double expected, const std::string& message) {
    require(std::abs(actual - expected) <= 1e-9 * std::max(1.0, std::abs(expected)), message);
}
template<class Exception, class F> void throws(F action, const std::string& message) {
    try { action(); } catch (const Exception&) { return; }
    throw std::runtime_error(message);
}
inline cps::Job job(const std::string& id, int pages = 1, cps::TimeMs submitted = 0, int priority = 0) {
    cps::Job result;
    result.id = id; result.title = id; result.pages = pages;
    result.submittedMs = submitted; result.priority = priority;
    return result;
}
inline cps::Job claim(cps::Scheduler& scheduler, cps::Policy policy, cps::TimeMs now) {
    const auto result = scheduler.claim(policy, now);
    require(result.has_value(), "expected a claimable job");
    return *result;
}
// One-shot gate to exercise true contention without real-time sleep assertions.
class Gate {
public:
    explicit Gate(int count) : remaining_(count) {}
    void arrive() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (--remaining_ == 0) condition_.notify_all();
        else condition_.wait(lock, [this] { return remaining_ == 0; });
    }
private:
    int remaining_;
    std::mutex mutex_;
    std::condition_variable condition_;
};
inline int run(const std::vector<std::pair<const char*, std::function<void()>>>& cases) {
    int failures = 0;
    for (const auto& item : cases) {
        try { item.second(); std::cout << "PASS " << item.first << '\n'; }
        catch (const std::exception& error) {
            ++failures; std::cerr << "FAIL " << item.first << ": " << error.what() << '\n';
        }
    }
    std::cout << cases.size() - static_cast<std::size_t>(failures) << '/' << cases.size() << " passed\n";
    return failures == 0 ? 0 : 1;
}
}
