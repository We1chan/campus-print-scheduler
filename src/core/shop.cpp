#include "cps/core/shop.hpp"
#include <algorithm>
#include <stdexcept>

namespace cps {
bool validPolicy(Policy p) {
    return p == Policy::Fcfs || p == Policy::ShortestJobFirst || p == Policy::PriorityAging ||
        p == Policy::Deadline || p == Policy::Revenue;
}
bool validTerms(const Job& j) {
    return (j.paper == PaperSize::A4 || j.paper == PaperSize::A3) &&
        (j.service == ServiceTier::Express || j.service == ServiceTier::Standard || j.service == ServiceTier::Flexible) &&
        j.deadlineMs >= 0 && j.deadlineMs <= MaxTimeMs && (j.deadlineMs == 0 || j.deadlineMs >= j.submittedMs) &&
        j.baseCents >= 0 && j.baseCents <= 3000000 && j.quotedCents >= 0 && j.quotedCents <= j.baseCents &&
        j.costCents >= 0 && j.costCents <= 3000000 && j.refundCents >= 0 && j.refundCents <= j.quotedCents &&
        (j.refundCents == 0 || j.state == JobState::Completed) && j.assignedPrinter >= -1 && j.assignedPrinter < 64 &&
        (j.payment == PaymentMethod::Counter || j.payment == PaymentMethod::Wechat ||
         j.payment == PaymentMethod::Alipay || j.payment == PaymentMethod::CampusCard) && j.customer.size() <= 256;
}
bool validPrinter(const Printer& p) {
    return !p.name.empty() && p.name.size() <= 200 && p.monoPageMs >= 10 && p.monoPageMs <= 5000 &&
        p.colorPageMs >= 10 && p.colorPageMs <= 5000;
}
bool compatible(const Job& j, const Printer& p) {
    return p.online && (j.paper != PaperSize::A3 || p.a3) && (!j.color || p.color) && (!j.duplex || p.duplex);
}
TimeMs serviceWindowMs(ServiceTier tier) {
    switch (tier) {
    case ServiceTier::Express: return 30 * 60000;
    case ServiceTier::Standard: return 120 * 60000;
    case ServiceTier::Flexible: return 480 * 60000;
    }
    throw std::invalid_argument("unknown service tier");
}
void quote(Job& j) {
    if (j.pages < 1 || j.pages > 1000 || j.copies < 1 || j.copies > 100 || j.sheets() > 10000 ||
        j.submittedMs < 0 || j.submittedMs > MaxTimeMs - serviceWindowMs(j.service))
        throw std::invalid_argument("invalid order for quote");
    j.baseCents = Money(j.sheets()) * (j.color ? 150 : 20) * (j.paper == PaperSize::A3 ? 2 : 1);
    const int discount = j.service == ServiceTier::Express ? 100 : j.service == ServiceTier::Standard ? 95 : 85;
    j.quotedCents = (j.baseCents * discount + 50) / 100;
    j.deadlineMs = j.submittedMs + serviceWindowMs(j.service);
}
TimeMs processingMs(const Job& j, const Printer& p) {
    return (j.color ? p.colorPageMs : p.monoPageMs) * j.sheets() * (j.paper == PaperSize::A3 ? 2 : 1);
}
Money directCost(const Job& j, const Printer& p) {
    return Money(j.paperSheets()) * (j.paper == PaperSize::A3 ? 6 : 3) +
        Money(j.sheets()) * (j.color ? 45 : p.color ? 7 : 5) * (j.paper == PaperSize::A3 ? 2 : 1);
}
std::vector<Printer> defaultFleet(TimeMs base) {
    base = std::clamp<TimeMs>(base, 10, 1250);
    return {{"黑白快印 01", false, false, true, true, base, base},
            {"黑白快印 02", false, false, true, true, base * 2, base * 2},
            {"黑白单面 03", false, false, false, true, base * 3, base * 3},
            {"彩色双面 04", false, true, true, true, base * 2, base * 3},
            {"彩色快印 05", false, true, true, true, base, base * 2},
            {"A3 全能 06", true, true, true, true, base * 2, base * 4}};
}
Inventory availablePaper(const std::vector<Job>& jobs, Inventory supplied) {
    for (const auto& j : jobs) {
        // Atomic simulated output: a retry reuses its reservation; failed/cancelled
        // attempts have no committed paper consumption in this model.
        if (j.attempts == 0 || j.state == JobState::Failed || j.state == JobState::Cancelled) continue;
        (j.paper == PaperSize::A3 ? supplied.a3 : supplied.a4) -= j.paperSheets();
    }
    return supplied;
}
ShopMetrics shopMetrics(const std::vector<Job>& jobs, TimeMs start, TimeMs end) {
    if (start < 0 || end < start || end > MaxTimeMs) throw std::invalid_argument("invalid shop metrics window");
    ShopMetrics m;
    for (const auto& j : jobs) {
        if (j.state == JobState::Completed && j.finishedMs && *j.finishedMs >= start && *j.finishedMs <= end) {
            ++m.completed;
            m.grossCents += j.quotedCents; m.refundCents += j.refundCents; m.costCents += j.costCents;
            m.paidCents[static_cast<std::size_t>(j.payment)] += j.quotedCents - j.refundCents;
            if (j.deadlineMs > 0) { if (*j.finishedMs <= j.deadlineMs) ++m.onTime; else ++m.late; }
        }
        if (j.submittedMs <= end && (j.state == JobState::Queued || j.state == JobState::Running || j.state == JobState::Cancelling)) {
            m.pendingCents += j.quotedCents;
            if (j.payment != PaymentMethod::Counter) m.prepaidCents += j.quotedCents;
            if (j.deadlineMs > 0 && j.deadlineMs < end) ++m.overdue;
        }
    }
    m.revenueCents = m.grossCents - m.refundCents;
    m.contributionCents = m.revenueCents - m.costCents;
    return m;
}
} // namespace cps
