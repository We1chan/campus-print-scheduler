#pragma once
#include "cps/core/shop.hpp"
#include <QJsonObject>
namespace cps {
QJsonObject orderTermsToJson(const Job& job);
bool orderTermsFromJson(const QJsonObject& object, Job& job, bool snapshot = false);
QJsonObject printerToJson(const Printer& printer);
bool printerFromJson(const QJsonObject& object, Printer& printer);
QJsonObject shopMetricsToJson(const ShopMetrics& metrics);
} // namespace cps
