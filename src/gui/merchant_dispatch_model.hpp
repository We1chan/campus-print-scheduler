#pragma once

#include "cps/core/shop_replay.hpp"
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <array>

namespace cps::gui::merchant {

struct DispatchDemoOrder {
    QString id;
    QString title;
    QColor color;
    cps::Job job;
    QString label = {};
};

struct DispatchDemoRun {
    QString label;
    QString explanation;
    cps::Policy policy = cps::Policy::Fcfs;
    cps::ShopReplayResult result;
    double averageWaitMinutes = 0;
    // Completed orders with finish <= deadline; no deadline also counts on time.
    int onTime = 0;
    cps::ShopQuality quality;
};

struct DispatchDemo {
    QVector<DispatchDemoOrder> orders;
    std::array<DispatchDemoRun, 3> runs;
    cps::TimeMs horizonMs = 0;
    QString scenarioName;
    bool business = false;
    unsigned seed = 0;
    std::vector<cps::Printer> fleet;
    cps::Inventory supplied{1000000, 1000000};
    bool classroom = false;
    cps::TimeMs originMs = 0;
    QJsonObject sourceSnapshot;
};

struct DispatchTimeline {
    cps::TimeMs startMs = 0, endMs = 0;
    std::array<std::vector<cps::Dispatch>, 3> runs;
};

// Shared rolling business window; each device shows at most its first three
// unfinished/upcoming dispatches. Full replay results and statistics stay intact.
DispatchTimeline dispatchBusinessTimeline(const DispatchDemo& demo, cps::TimeMs nowMs);

// Teaching scenarios use 3 pages/minute on one printer. Their entire timeline is scaled
// by four, including arrivals, deadlines, history and statistics. Thus the core
// 30-second aging interval is two minutes on the teaching clock.
// This computes local values only, without an IPC client or persisted queue.
// Business scenarios instead use the unscaled production clock and six devices.
DispatchDemo makeDispatchDemo(int scenario);
DispatchDemo makeClassroomDispatchDemo(const QJsonObject& snapshot);
QByteArray classroomDispatchFingerprint(const QJsonObject& snapshot);
QJsonObject dispatchDemoReport(const DispatchDemo& demo);
QString dispatchBusinessComparison(const DispatchDemo& demo);
QStringList dispatchScenarioNames();
const DispatchDemoOrder* findDispatchOrder(const DispatchDemo& demo, const std::string& id);

} // namespace cps::gui::merchant
