#include "cps/runtime/shop_io.hpp"
#include <algorithm>
#include <cmath>
namespace cps {
namespace {
bool number(const QJsonObject& o, const char* key, std::int64_t min, std::int64_t max, std::int64_t& out) {
    const auto v = o.value(QLatin1String(key)); const auto n = v.toDouble();
    if (!v.isDouble() || !std::isfinite(n) || n < double(min) || n > double(max) || std::floor(n) != n) return false;
    out = static_cast<std::int64_t>(n); return true;
}
}
QString paymentName(PaymentMethod method) {
    switch (method) {
    case PaymentMethod::Counter: return QStringLiteral("counter");
    case PaymentMethod::Wechat: return QStringLiteral("wechat");
    case PaymentMethod::Alipay: return QStringLiteral("alipay");
    case PaymentMethod::CampusCard: return QStringLiteral("campus_card");
    }
    return {};
}
std::optional<PaymentMethod> paymentFromName(const QString& name) {
    for (auto method : {PaymentMethod::Counter, PaymentMethod::Wechat, PaymentMethod::Alipay, PaymentMethod::CampusCard})
        if (name == paymentName(method)) return method;
    return std::nullopt;
}
QJsonObject orderTermsToJson(const Job& j) {
    QJsonObject terms{{"paper", j.paper == PaperSize::A3 ? "a3" : "a4"}, {"color", j.color}, {"duplex", j.duplex},
        {"service", j.service == ServiceTier::Express ? "express" : j.service == ServiceTier::Flexible ? "flexible" : "standard"},
        {"deadline_ms", double(j.deadlineMs)}, {"base_cents", double(j.baseCents)}, {"quoted_cents", double(j.quotedCents)},
        {"cost_cents", double(j.costCents)}, {"refund_cents", double(j.refundCents)}, {"assigned_printer", j.assignedPrinter}};
    // Counter orders without a customer are the default and stay out of large snapshots.
    if (j.payment != PaymentMethod::Counter) terms.insert("payment", paymentName(j.payment));
    if (!j.customer.empty()) terms.insert("customer", QString::fromUtf8(j.customer.data(), static_cast<qsizetype>(j.customer.size())));
    return terms;
}
bool orderTermsFromJson(const QJsonObject& o, Job& j, bool snapshot) {
    if (o.contains("paper")) {
        const auto p = o.value("paper").toString();
        if (p != "a4" && p != "a3") return false;
        j.paper = p == "a3" ? PaperSize::A3 : PaperSize::A4;
    }
    for (auto pair : {std::pair<const char*, bool*>{"color", &j.color}, {"duplex", &j.duplex}}) {
        if (o.contains(pair.first)) { if (!o.value(pair.first).isBool()) return false; *pair.second = o.value(pair.first).toBool(); }
    }
    if (o.contains("service")) {
        const auto s = o.value("service").toString();
        if (s != "express" && s != "standard" && s != "flexible") return false;
        j.service = s == "express" ? ServiceTier::Express : s == "flexible" ? ServiceTier::Flexible : ServiceTier::Standard;
    }
    if (o.contains("payment")) {
        const auto method = paymentFromName(o.value("payment").toString());
        if (!method) return false;
        j.payment = *method;
    }
    if (o.contains("customer")) {
        const auto name = o.value("customer").toString().trimmed();
        if (!o.value("customer").isString() || name.size() > 64 ||
            std::any_of(name.begin(), name.end(), [](QChar c) { return !c.isPrint(); })) return false;
        j.customer = name.toUtf8().toStdString();
    }
    const bool extended = o.contains("paper") || o.contains("service") || o.contains("quoted_cents") || o.contains("deadline_ms") || o.contains("assigned_printer");
    if (snapshot && extended) {
        std::int64_t device;
        if (!number(o, "deadline_ms", 0, MaxTimeMs, j.deadlineMs) || !number(o, "base_cents", 0, 3000000, j.baseCents) ||
            !number(o, "quoted_cents", 0, 3000000, j.quotedCents) || !number(o, "cost_cents", 0, 3000000, j.costCents) ||
            !number(o, "refund_cents", 0, 3000000, j.refundCents) || !number(o, "assigned_printer", -1, 63, device)) return false;
        j.assignedPrinter = static_cast<int>(device);
    }
    return validTerms(j);
}
QJsonObject printerToJson(const Printer& p) {
    return {{"name", QString::fromStdString(p.name)}, {"a3", p.a3}, {"color", p.color}, {"duplex", p.duplex},
        {"online", p.online}, {"mono_page_ms", double(p.monoPageMs)}, {"color_page_ms", double(p.colorPageMs)}};
}
bool printerFromJson(const QJsonObject& o, Printer& p) {
    p.name = o.value("name").toString().trimmed().toStdString();
    for (auto pair : {std::pair<const char*, bool*>{"a3", &p.a3}, {"color", &p.color}, {"duplex", &p.duplex}, {"online", &p.online}}) {
        if (!o.value(pair.first).isBool()) return false;
        *pair.second = o.value(pair.first).toBool();
    }
    return number(o, "mono_page_ms", 10, 10000, p.monoPageMs) &&
        number(o, "color_page_ms", 10, 10000, p.colorPageMs) && validPrinter(p);
}
QJsonObject shopMetricsToJson(const ShopMetrics& m) {
    return {{"gross_cents", double(m.grossCents)}, {"refund_cents", double(m.refundCents)},
        {"revenue_cents", double(m.revenueCents)}, {"cost_cents", double(m.costCents)},
        {"contribution_cents", double(m.contributionCents)}, {"pending_cents", double(m.pendingCents)},
        {"completed", m.completed}, {"on_time", m.onTime}, {"late", m.late}, {"overdue", m.overdue},
        {"prepaid_cents", double(m.prepaidCents)}, {"paid_cents", QJsonObject{
            {"counter", double(m.paidCents[0])}, {"wechat", double(m.paidCents[1])},
            {"alipay", double(m.paidCents[2])}, {"campus_card", double(m.paidCents[3])}}}};
}
} // namespace cps
