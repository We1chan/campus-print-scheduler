#pragma once
#include "cps/core/scheduler.hpp"

namespace cps {
bool validPolicy(Policy policy);
bool validTerms(const Job& job);
bool validPrinter(const Printer& printer);
bool compatible(const Job& job, const Printer& printer);
TimeMs serviceWindowMs(ServiceTier tier);
void quote(Job& job);
TimeMs processingMs(const Job& job, const Printer& printer);
Money directCost(const Job& job, const Printer& printer);
std::vector<Printer> defaultFleet(TimeMs basePageMs = 1500);
Inventory availablePaper(const std::vector<Job>& jobs, Inventory supplied);
struct ShopMetrics {
    Money grossCents = 0, refundCents = 0, revenueCents = 0, costCents = 0, contributionCents = 0;
    Money pendingCents = 0;
    int completed = 0, onTime = 0, late = 0, overdue = 0;
};
ShopMetrics shopMetrics(const std::vector<Job>& jobs, TimeMs start, TimeMs end);
} // namespace cps
