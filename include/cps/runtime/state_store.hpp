#pragma once

#include "cps/core/scheduler.hpp"

#include <QJsonObject>
#include <QString>
#include <optional>
#include <vector>

namespace cps {

struct PersistentState {
    SchedulerOptions schedulerOptions{};
    Policy policy = Policy::Fcfs;
    int printers = 2;
    TimeMs savedAtMs = 0;
    std::vector<Job> jobs;
};

QString policyName(Policy policy);
std::optional<Policy> policyFromName(const QString& name);

QJsonObject jobSnapshotToJson(const Job& job);
bool jobSnapshotFromJson(const QJsonObject& object, Job& job, QString& error);

bool writePersistentState(const QString& path, const PersistentState& state, QString& error);
bool readPersistentState(const QString& path, PersistentState& state, QString& error);

} // namespace cps
