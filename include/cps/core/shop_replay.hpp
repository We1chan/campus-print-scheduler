#pragma once
#include "cps/core/shop.hpp"
#include "cps/core/replay.hpp"
namespace cps {
struct ShopReplayResult {
    std::vector<Job> jobs;
    std::vector<Dispatch> dispatches;
    ShopMetrics business;
    Statistics performance;
};
// Locked orders, common clock [0, horizon], no sleeps or customer assumptions.
ShopReplayResult replayShop(const std::vector<Job>& orders, Policy policy,
                            const std::vector<Printer>& fleet, TimeMs horizon,
                            Inventory supplied = {1000000, 1000000});
} // namespace cps
