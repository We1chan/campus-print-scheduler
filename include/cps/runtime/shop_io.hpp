#pragma once
#include "cps/core/shop.hpp"
#include <QJsonObject>
#include <optional>
namespace cps {
QString paymentName(PaymentMethod method);
std::optional<PaymentMethod> paymentFromName(const QString& name);
QJsonObject orderTermsToJson(const Job& job);
bool orderTermsFromJson(const QJsonObject& object, Job& job, bool snapshot = false);
QJsonObject printerToJson(const Printer& printer);
bool printerFromJson(const QJsonObject& object, Printer& printer);
QJsonObject shopMetricsToJson(const ShopMetrics& metrics);
} // namespace cps
