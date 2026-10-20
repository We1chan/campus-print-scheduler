#pragma once
#include "cps/core/scheduler.hpp"
#include <array>

namespace cps {
bool validPolicy(Policy policy);
bool validTerms(const Job& job);
bool validPrinter(const Printer& printer);
bool compatible(const Job& job, const Printer& printer);
TimeMs serviceWindowMs(ServiceTier tier);
struct Pricing {
    Money monoFaceCents = 20, colorFaceCents = 150;
};
inline constexpr Pricing DemoPricing{10, 75};
void quote(Job& job, Pricing pricing = {});
TimeMs processingMs(const Job& job, const Printer& printer);
Money directCost(const Job& job, const Printer& printer);
std::vector<Printer> defaultFleet(TimeMs basePageMs = 1250);
Inventory availablePaper(const std::vector<Job>& jobs, Inventory supplied);
struct ShopMetrics {
    Money grossCents = 0, refundCents = 0, revenueCents = 0, costCents = 0, contributionCents = 0;
    Money pendingCents = 0;
    Money prepaidCents = 0;                 // paid online, order not finished yet
    std::array<Money, 4> paidCents{};       // settled revenue by PaymentMethod
    int completed = 0, onTime = 0, late = 0, overdue = 0;
};
ShopMetrics shopMetrics(const std::vector<Job>& jobs, TimeMs start, TimeMs end);
// Controlled comparisons: same accepted cohort, horizon, fleet and pricing.
// Includes unstarted documents in wait observations through the window end.
struct ShopQuality {
    double averageObservedWaitMs = 0, p95ObservedWaitMs = 0;
    int expressAccepted = 0, expressOnTime = 0;
    Money potentialContributionCents = 0;
    double waitingScore = 0, expressOnTimeRate = 0, contributionScore = 0;
    double overallScore = 0; // 0..100; three equally weighted components
};
ShopQuality shopQuality(const std::vector<Job>& jobs, TimeMs start, TimeMs end);
} // namespace cps
