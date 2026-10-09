#pragma once

#include "cps/core/shop_replay.hpp"
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <array>

namespace cps::gui::merchant {

struct DispatchDemoOrder {
    QString id;
    QString title;
    QColor color;
    cps::Job job;
};

struct DispatchDemoRun {
    QString label;
    QString explanation;
    cps::Policy policy = cps::Policy::Fcfs;
    cps::ShopReplayResult result;
    double averageWaitMinutes = 0;
    // Completed orders with finish <= deadline; no deadline also counts on time.
    int onTime = 0;
};

struct DispatchDemo {
    QVector<DispatchDemoOrder> orders;
    std::array<DispatchDemoRun, 3> runs;
    cps::TimeMs horizonMs = 0;
    QString scenarioName;
};

// All exposed timestamps use one teaching clock: 3 pages/minute, one printer.
// replayShop uses its valid 5-second/page printer; its entire timeline is scaled
// by four, including arrivals, deadlines, history and statistics. Thus the core
// 30-second aging interval is two minutes on the teaching clock.
// This computes local values only, without an IPC client or persisted queue.
DispatchDemo makeDispatchDemo(int scenario);
QStringList dispatchScenarioNames();
const DispatchDemoOrder* findDispatchOrder(const DispatchDemo& demo, const std::string& id);

} // namespace cps::gui::merchant
