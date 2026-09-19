#pragma once

#include "cps/core/scheduler.hpp"
#include <QJsonObject>
#include <QString>

namespace cps {
bool readJob(const QString& path, Job& job, QString& error);
bool writeJob(const QString& path, const Job& job, QString& error);
void emitEvent(const QJsonObject& event);
QString siblingExecutable(const QString& name);
QString defaultStateDirectory();
} // namespace cps
