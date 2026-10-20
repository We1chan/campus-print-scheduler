#include "merchant_dispatch_model.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace cps::gui::merchant {
namespace {
constexpr cps::TimeMs Minute = 60000;
constexpr cps::TimeMs ClockScale = 4;
constexpr cps::TimeMs CorePageMs = 5000;

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

qint64 integer(const QJsonObject& object, const char* name, qint64 low = 0,
               qint64 high = cps::MaxTimeMs) {
    const auto value = object.value(QLatin1String(name));
    const auto n = value.toDouble(-1);
    require(value.isDouble() && std::isfinite(n) && std::floor(n) == n && n >= low && n <= high,
            "report contains an invalid integer");
    return static_cast<qint64>(n);
}

bool boolean(const QJsonObject& object, const char* name) {
    const auto value = object.value(QLatin1String(name));
    require(value.isBool(), "report contains an invalid boolean");
    return value.toBool();
}

double fractionValue(const QString& value) {
    const auto parts = value.split('/');
    bool numeratorOk = false, denominatorOk = true;
    const double numerator = parts.value(0).toDouble(&numeratorOk);
    const double denominator = parts.size() == 2 ? parts[1].toDouble(&denominatorOk) : 1.;
    require(parts.size() <= 2 && numeratorOk && denominatorOk && denominator > 0 &&
            std::isfinite(numerator) && std::isfinite(denominator), "invalid proof fraction");
    return numerator / denominator;
}

void summarize(DispatchDemoRun& run, cps::TimeMs horizon, int printers) {
    run.result.performance = cps::statistics(run.result.jobs, 0, horizon, printers);
    run.result.business = cps::shopMetrics(run.result.jobs, 0, horizon);
    run.quality = cps::shopQuality(run.result.jobs, 0, horizon);
    run.averageWaitMinutes = run.quality.averageObservedWaitMs / Minute;
    run.onTime = 0;
    for (const auto& job : run.result.jobs)
        if (job.state == cps::JobState::Completed && job.finishedMs &&
            (job.deadlineMs == 0 || *job.finishedMs <= job.deadlineMs)) ++run.onTime;
}

void checkQuality(const QJsonObject& expected, const DispatchDemoRun& run) {
    require(!expected.isEmpty(), "missing reported quality");
    const std::array<std::pair<const char*, double>, 11> values{{
        {"average_observed_wait_ms", run.quality.averageObservedWaitMs},
        {"p95_observed_wait_ms", run.quality.p95ObservedWaitMs},
        {"express_accepted", run.quality.expressAccepted},
        {"express_on_time", run.quality.expressOnTime},
        {"express_on_time_rate", run.quality.expressOnTimeRate},
        {"potential_contribution_cents", double(run.quality.potentialContributionCents)},
        {"contribution_cents", double(run.result.business.contributionCents)},
        {"waiting_score", run.quality.waitingScore},
        {"contribution_score", run.quality.contributionScore},
        {"overall_score", run.quality.overallScore},
        {"completed", run.result.business.completed}}};
    for (const auto& item : values) {
        const auto value = expected.value(QLatin1String(item.first));
        require(value.isDouble() && std::isfinite(value.toDouble()) &&
                std::abs(value.toDouble() - item.second) <= 1e-7 * std::max(1., std::abs(item.second)),
                "reported quality differs from recomputed schedule quality");
    }
}

cps::ShopReplayResult scheduledResult(const std::vector<cps::Job>& orders,
        const std::vector<cps::Printer>& fleet, const QJsonArray& dispatches,
        cps::TimeMs horizon, cps::Inventory inventory, bool validateDuration = true) {
    cps::ShopReplayResult result;
    result.jobs = orders;
    for (auto& job : result.jobs) {
        job.queuedSinceMs = job.submittedMs;
        job.lastChangedMs = job.submittedMs;
    }
    QSet<QString> seen;
    std::vector<std::vector<std::pair<cps::TimeMs, cps::TimeMs>>> intervals(fleet.size());
    qint64 usedA4 = 0, usedA3 = 0;
    for (const auto& value : dispatches) {
        require(value.isObject(), "invalid dispatch object");
        const auto row = value.toObject();
        const auto id = row.value("job_id").toString();
        require(!id.isEmpty() && !seen.contains(id), "duplicate or empty dispatch ID");
        seen.insert(id);
        const auto found = std::find_if(result.jobs.begin(), result.jobs.end(),
            [&id](const cps::Job& job) { return QString::fromStdString(job.id) == id; });
        require(found != result.jobs.end(), "unknown dispatched order");
        auto& job = *found;
        const int printer = int(integer(row, "printer", 0, qint64(fleet.size()) - 1));
        const auto start = integer(row, "started_ms", job.submittedMs, horizon - 1);
        const auto finish = integer(row, "finished_ms", start + 1);
        require(cps::compatible(job, fleet[printer]), "dispatch uses an incompatible printer");
        require(!validateDuration || finish - start == cps::processingMs(job, fleet[printer]),
                "dispatch processing duration differs from core model");
        intervals[printer].push_back({start, finish});
        (job.paper == cps::PaperSize::A4 ? usedA4 : usedA3) += job.paperSheets();
        job.assignedPrinter = printer;
        job.costCents = cps::directCost(job, fleet[printer]);
        job.attempts = 1;
        job.firstStartedMs = start;
        job.accumulatedWaitMs = start - job.submittedMs;
        job.lastChangedMs = finish <= horizon ? finish : start;
        job.state = finish <= horizon ? cps::JobState::Completed : cps::JobState::Running;
        job.runs.push_back({start, finish <= horizon ? std::optional<cps::TimeMs>(finish) : std::nullopt});
        if (finish <= horizon) job.finishedMs = finish;
        result.dispatches.push_back({job.id, printer, start, finish});
    }
    require(usedA4 <= inventory.a4 && usedA3 <= inventory.a3, "report exceeds paper inventory");
    for (auto& machine : intervals) {
        std::sort(machine.begin(), machine.end());
        cps::TimeMs free = 0;
        for (const auto& interval : machine) {
            require(interval.first >= free, "report double books a printer");
            free = interval.second;
        }
    }
    cps::Scheduler validator;
    require(validator.restore(result.jobs, horizon), "report produces an invalid job history");
    std::stable_sort(result.dispatches.begin(), result.dispatches.end(), [](const auto& a, const auto& b) {
        return std::make_pair(a.startedMs, a.printer) < std::make_pair(b.startedMs, b.printer);
    });
    return result;
}

void scaleJobTime(cps::Job& job, cps::TimeMs factor) {
    job.submittedMs *= factor;
    job.deadlineMs *= factor;
    job.queuedSinceMs *= factor;
    job.accumulatedWaitMs *= factor;
    job.lastChangedMs *= factor;
    if (job.firstStartedMs) *job.firstStartedMs *= factor;
    if (job.finishedMs) *job.finishedMs *= factor;
    for (auto& interval : job.runs) {
        interval.startedMs *= factor;
        if (interval.stoppedMs) *interval.stoppedMs *= factor;
    }
}

std::vector<cps::Job> coreOrders(int scenario) {
    const std::array<const char*, 6> ids{{"A", "B", "C", "D", "E", "F"}};
    const std::array<QString, 6> titles{{QStringLiteral("报告"), QStringLiteral("作业"),
        QStringLiteral("讲义"), QStringLiteral("表格"), QStringLiteral("材料"), QStringLiteral("简历")}};
    const std::array<int, 6> pages{{12, 2, 6, 3, 8, 1}};
    // Times here are in teaching minutes. Zero means no delivery deadline.
    std::array<int, 6> deadlineMinutes{{12, 3, 8, 5, 0, 2}};
    std::array<int, 6> arrivalMinutes{};
    if (scenario == 1) {
        deadlineMinutes = {{4, 2, 8, 5, 10, 1}};
    } else if (scenario == 2) {
        arrivalMinutes = {{0, 0, 1, 2, 3, 4}};
        deadlineMinutes = {{9, 6, 8, 7, 0, 6}};
    } else if (scenario == 3) {
        arrivalMinutes = {{0, 0, 0, 1, 1, 2}};
        deadlineMinutes = {{11, 3, 8, 5, 0, 4}};
    }
    std::vector<cps::Job> result;
    result.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        cps::Job job;
        job.id = ids[index];
        job.title = titles[index].toUtf8().toStdString();
        job.pages = pages[index];
        job.submittedMs = arrivalMinutes[index] * Minute / ClockScale;
        // Both print modes have the same demo speed; prices use the real quote.
        job.color = index == 3 || index == 5 || (scenario == 3 && index == 4);
        job.service = (index == 0 || index == 4) ? cps::ServiceTier::Flexible :
            (index == 2 ? cps::ServiceTier::Standard : cps::ServiceTier::Express);
        cps::quote(job);
        // Teaching pickup promises deliberately replace quote's default hours.
        job.deadlineMs = deadlineMinutes[index] * Minute / ClockScale;
        result.push_back(std::move(job));
    }
    return result;
}
} // namespace

QStringList dispatchScenarioNames() {
    return {QStringLiteral("长单与短单"), QStringLiteral("临近取件期限"),
        QStringLiteral("服务档位与等待老化"), QStringLiteral("营业高峰与收益"),
        QStringLiteral("100 单极限压力")};
}

DispatchDemo makeDispatchDemo(int scenario) {
    const auto names = dispatchScenarioNames();
    if (scenario < 0 || scenario >= names.size()) scenario = 0;
    if (scenario == 4) {
        QString path = qEnvironmentVariable("CPS_STRESS_REPORT");
        if (path.isEmpty()) {
            QDir directory(QDir::currentPath());
            for (int level = 0; level < 6; ++level) {
                const auto candidate = directory.filePath("runtime/stress100/report.json");
                if (QFile::exists(candidate)) { path = candidate; break; }
                if (!directory.cdUp()) break;
            }
            if (path.isEmpty()) path = QDir::current().filePath("runtime/stress100/report.json");
        }
        return loadDispatchReport(path);
    }
    DispatchDemo demo;
    demo.scenarioName = names[scenario];
    demo.printers = {QStringLiteral("教学打印机")};
    const auto orders = coreOrders(scenario);
    const std::array<const char*, 6> colors{{"#A5CAEF", "#A6C398", "#ECD092",
        "#E4A6AB", "#BDB1E0", "#8CCBD0"}};
    cps::TimeMs lastArrival = 0;
    cps::TimeMs totalProcessing = 0;
    const std::vector<cps::Printer> fleet{{"教学打印机", false, true, true, true, CorePageMs, CorePageMs}};
    for (std::size_t index = 0; index < orders.size(); ++index) {
        auto displayed = orders[index];
        scaleJobTime(displayed, ClockScale);
        demo.orders.push_back({QString::fromStdString(displayed.id), QString::fromStdString(displayed.title),
            QColor(QString::fromLatin1(colors[index])), displayed});
        lastArrival = std::max(lastArrival, orders[index].submittedMs);
        totalProcessing += cps::processingMs(orders[index], fleet.front());
    }
    // Any work-conserving ordering finishes by last arrival + total service.
    // Share this conservative bound across all three rows, including idle time.
    const auto coreHorizon = lastArrival + totalProcessing;
    demo.horizonMs = coreHorizon * ClockScale;
    const std::array<cps::Policy, 2> policies{{cps::Policy::Fcfs, cps::Policy::ShortestJobFirst}};
    const std::array<QString, 3> keys{{"fcfs", "sjf", "optimal"}};
    const std::array<QString, 3> labels{{QStringLiteral("FCFS · 先来先服务"), QStringLiteral("SJF · 短任务优先"), QStringLiteral("全局最优")}};
    const std::array<QString, 3> explanations{{
        QStringLiteral("队列按到达顺序，依次分配空闲兼容机；已开始的订单继续打印。"),
        QStringLiteral("队列按打印面数从少到多，分配最快兼容机，缩短短单等待。"),
        QStringLiteral("已知整批订单后，按等待、快速准时和贡献收益等权求解；允许主动空闲。")}};
    for (std::size_t index = 0; index < demo.runs.size(); ++index) {
        auto& run = demo.runs[index];
        run.key = keys[index];
        run.label = labels[index];
        run.explanation = explanations[index] + QStringLiteral(" 无取件期限的完成订单计为按时。教学时钟：核心 5 秒/页 × 4，即 3 页/分钟。");
        if (index == 2) {
            // Fixed traces from the existing exact rational enumeration, not a
            // new online policy or a second implementation of the solver.
            const std::array<QStringList, 4> sequences{{
                {"F", "B", "D", "C", "E", "A"}, {"F", "B", "D", "C", "E", "A"},
                {"B", "C", "D", "F", "A", "E"}, {"B", "C", "F", "D", "E", "A"}}};
            const std::array<QString, 4> exactScores{{"100932850/1085139", "100932850/1085139",
                "722985500/7707843", "374843450/3955149"}};
            std::vector<cps::Job> displayed;
            for (const auto& item : demo.orders) displayed.push_back(item.job);
            QJsonArray dispatches;
            cps::TimeMs free = 0;
            for (const auto& id : sequences[scenario]) {
                const auto* item = findDispatchOrder(demo, id.toStdString());
                const auto start = std::max(free, item->job.submittedMs);
                free = start + item->job.sheets() * CorePageMs * ClockScale;
                dispatches.append(QJsonObject{{"job_id", id}, {"printer", 0},
                    {"started_ms", start}, {"finished_ms", free}});
            }
            run.result = scheduledResult(displayed, fleet, dispatches, demo.horizonMs, {10000, 3000}, false);
            summarize(run, demo.horizonMs, 1);
            require(std::abs(run.quality.overallScore - fractionValue(exactScores[scenario])) < 1e-9,
                    "fixed teaching optimum differs from its exact score");
            run.status = "OPTIMAL";
            run.provenOptimal = true;
            run.proofSummary = QStringLiteral("精确枚举 1957 个排程；上下界相同，差距 0。");
            continue;
        }
        run.policy = policies[index];
        run.status = "BASELINE";
        run.result = cps::replayShop(orders, run.policy, fleet, coreHorizon);
        for (auto& job : run.result.jobs) scaleJobTime(job, ClockScale);
        for (auto& dispatch : run.result.dispatches) {
            dispatch.startedMs *= ClockScale;
            dispatch.finishedMs *= ClockScale;
        }
        summarize(run, demo.horizonMs, 1);
    }
    return demo;
}

DispatchDemo loadDispatchReport(const QString& path) {
    DispatchDemo demo;
    demo.stress = true;
    demo.scenarioName = QStringLiteral("100 单极限压力");
    demo.reportPath = path;
    demo.horizonMs = Minute;
    const std::array<QString, 3> keys{{"fcfs", "sjf", "optimal"}};
    const std::array<QString, 3> labels{{QStringLiteral("FCFS · 先来先服务"),
        QStringLiteral("SJF · 短任务优先"), QStringLiteral("最优求解 · 待生成")}};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        demo.runs[i].key = keys[i];
        demo.runs[i].label = labels[i];
    }
    try {
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "report file has not been generated");
        require(file.size() <= 16 * 1024 * 1024, "report exceeds 16 MiB");
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        require(error.error == QJsonParseError::NoError && document.isObject(), "invalid report JSON");
        const auto root = document.object();
        require(root.value("kind") == "campus-print-stress-comparison" &&
                integer(root, "schema_version", 1, 1) == 1, "unsupported comparison report schema");
        const auto scene = root.value("scenario").toObject();
        require(!scene.isEmpty(), "missing report scenario");
        QByteArray scenarioBytes;
        if (root.value("scenario_json").isString()) {
            scenarioBytes = root.value("scenario_json").toString().toUtf8();
            const auto parsed = QJsonDocument::fromJson(scenarioBytes, &error);
            require(error.error == QJsonParseError::NoError && parsed.isObject() && parsed.object() == scene,
                    "scenario_json differs from scenario");
        } else scenarioBytes = QJsonDocument(scene).toJson(QJsonDocument::Compact);
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(scenarioBytes, QCryptographicHash::Sha256).toHex());
        require(root.value("scenario_sha256").toString() == hash, "scenario input hash mismatch");
        const auto horizon = integer(scene, "horizon_ms", 1);
        const auto jobs = scene.value("jobs").toArray();
        require(jobs.size() == 100, "stress comparison must contain exactly 100 orders");
        const auto machines = scene.value("printers").toArray();
        require(!machines.isEmpty() && machines.size() <= 64, "invalid comparison fleet");
        std::vector<cps::Printer> fleet;
        for (const auto& value : machines) {
            require(value.isObject(), "invalid printer object");
            const auto machine = value.toObject();
            cps::Printer printer;
            printer.name = machine.value("name").toString().toUtf8().toStdString();
            printer.online = boolean(machine, "online"); printer.a3 = boolean(machine, "a3");
            printer.color = boolean(machine, "color"); printer.duplex = boolean(machine, "duplex");
            printer.monoPageMs = integer(machine, "mono_page_ms", 10, 5000);
            printer.colorPageMs = integer(machine, "color_page_ms", 10, 5000);
            require(cps::validPrinter(printer), "invalid printer configuration");
            fleet.push_back(printer);
        }
        std::vector<cps::Job> orders;
        QSet<QString> ids;
        const auto durations = scene.value("processing_ms").toArray();
        const auto costs = scene.value("cost_cents").toArray();
        require(durations.size() == jobs.size() && costs.size() == jobs.size(), "invalid matrix row count");
        for (qsizetype index = 0; index < jobs.size(); ++index) {
            require(jobs[index].isObject(), "invalid order object");
            const auto row = jobs[index].toObject();
            const auto id = row.value("job_id").toString();
            require(!id.isEmpty() && !ids.contains(id), "duplicate or empty order ID"); ids.insert(id);
            cps::Job job;
            job.id = id.toStdString(); job.title = row.value("title").toString().toUtf8().toStdString();
            job.pages = int(integer(row, "pages", 1, 1000)); job.copies = int(integer(row, "copies", 1, 100));
            require(job.sheets() <= 10000, "too many printed faces");
            job.submittedMs = integer(row, "submitted_ms", 0, horizon);
            job.queuedSinceMs = job.lastChangedMs = job.submittedMs;
            require(row.value("paper") == "a4" || row.value("paper") == "a3", "invalid paper type");
            job.paper = row.value("paper") == "a3" ? cps::PaperSize::A3 : cps::PaperSize::A4;
            job.color = boolean(row, "color"); job.duplex = boolean(row, "duplex");
            const auto service = row.value("service").toString();
            require(service == "express" || service == "standard" || service == "flexible", "invalid service tier");
            job.service = service == "express" ? cps::ServiceTier::Express : service == "standard" ?
                cps::ServiceTier::Standard : cps::ServiceTier::Flexible;
            job.deadlineMs = integer(row, "deadline_ms");
            job.baseCents = integer(row, "base_cents", 0, 3000000);
            job.quotedCents = integer(row, "quoted_cents", 0, 3000000);
            require(integer(row, "refund_cents", 0, 0) == 0 && cps::validTerms(job), "invalid commercial terms");
            require(integer(row, "nominal_cost_cents", 0, 3000000) == cps::directCost(job, cps::Printer{}),
                    "nominal cost differs from core model");
            const auto processingRow = durations[index].toArray(), costRow = costs[index].toArray();
            require(processingRow.size() == machines.size() && costRow.size() == machines.size(), "invalid matrix column count");
            for (qsizetype m = 0; m < machines.size(); ++m) {
                if (!cps::compatible(job, fleet[std::size_t(m)])) {
                    require(processingRow[m].isNull() && costRow[m].isNull(), "incompatible matrix entry");
                } else {
                    require(processingRow[m].isDouble() && costRow[m].isDouble() &&
                            processingRow[m].toDouble() == double(cps::processingMs(job, fleet[std::size_t(m)])) &&
                            costRow[m].toDouble() == double(cps::directCost(job, fleet[std::size_t(m)])),
                            "processing or cost matrix differs from core model");
                }
            }
            orders.push_back(job);
        }
        const auto stock = scene.value("inventory").toObject();
        const cps::Inventory inventory{int(integer(stock, "a4", 0, std::numeric_limits<int>::max())),
            int(integer(stock, "a3", 0, std::numeric_limits<int>::max()))};
        const auto runs = root.value("runs").toArray();
        require(runs.size() == 3, "comparison requires exactly FCFS, SJF and one solver result");
        QSet<QString> policies;
        auto validated = demo;
        validated.horizonMs = horizon;
        validated.scenarioName = scene.value("name").toString(demo.scenarioName);
        for (const auto& printer : fleet) validated.printers << QString::fromStdString(printer.name);
        for (qsizetype i = 0; i < jobs.size(); ++i) {
            const auto& job = orders[std::size_t(i)];
            validated.orders.push_back({QString::fromStdString(job.id), QString::fromStdString(job.title),
                QColor::fromHsv(int((i * 137) % 360), 64, 230), job});
        }
        for (const auto& value : runs) {
            require(value.isObject(), "invalid comparison run");
            const auto row = value.toObject(); const auto key = row.value("policy").toString();
            require((key == "fcfs" || key == "sjf" || key == "optimal") && !policies.contains(key),
                    "unknown or duplicate comparison policy"); policies.insert(key);
            const std::size_t index = key == "fcfs" ? 0 : key == "sjf" ? 1 : 2;
            auto& run = validated.runs[index];
            require(row.value("dispatches").isArray(), "missing dispatch list");
            run.result = scheduledResult(orders, fleet, row.value("dispatches").toArray(), horizon, inventory);
            summarize(run, horizon, int(fleet.size()));
            checkQuality(row.value("quality").toObject(), run);
            run.status = row.value("status").toString();
            if (index < 2) {
                run.policy = index == 0 ? cps::Policy::Fcfs : cps::Policy::ShortestJobFirst;
                require(run.status == "BASELINE", "invalid baseline status");
                auto replay = cps::replayShop(orders, run.policy, fleet, horizon, inventory);
                std::stable_sort(replay.dispatches.begin(), replay.dispatches.end(), [](const auto& a, const auto& b) {
                    return std::make_pair(a.startedMs, a.printer) < std::make_pair(b.startedMs, b.printer);
                });
                require(replay.dispatches.size() == run.result.dispatches.size(), "baseline dispatch count differs from core replay");
                for (std::size_t i = 0; i < replay.dispatches.size(); ++i) {
                    const auto& a = replay.dispatches[i]; const auto& b = run.result.dispatches[i];
                    require(a.jobId == b.jobId && a.printer == b.printer && a.startedMs == b.startedMs && a.finishedMs == b.finishedMs,
                            "baseline dispatch differs from core replay");
                }
                run.explanation = index == 0 ? QStringLiteral("按到达顺序派发，选择编号最小的空闲兼容设备。") :
                    QStringLiteral("先打印面数少的订单，再选择最快的空闲兼容设备。");
            } else {
                require(run.status == "OPTIMAL" || run.status == "FEASIBLE", "invalid solver status");
                const auto proof = row.value("proof").toObject();
                const auto lower = proof.value("lower_bound_fraction").toString();
                const auto upper = proof.value("upper_bound_fraction").toString();
                run.provenOptimal = run.status == "OPTIMAL" && proof.value("complete").toBool(false) &&
                    !lower.isEmpty() && lower == upper &&
                    proof.value("absolute_gap_fraction").toString() == "0" &&
                    std::abs(fractionValue(lower) - run.quality.overallScore) < 1e-7;
                run.label = run.provenOptimal ? QStringLiteral("全局最优 · 已证明") : QStringLiteral("最优求解 · 当前最好");
                run.proofSummary = run.provenOptimal ? QStringLiteral("报告上下界相等，差距 0；已复核输入、排程和得分。") :
                    QStringLiteral("时间表已复核；尚未取得全局最优证明。");
                run.explanation = QStringLiteral("已知全部 100 单后离线求解，允许主动空闲。") + run.proofSummary;
            }
        }
        require(policies.size() == 3, "missing comparison policy");
        validated.notice = QStringLiteral("固定 100 单 · 共同设备与库存 · 输入哈希和三条时间表已复核");
        return validated;
    } catch (const std::exception& error) {
        demo.notice = QStringLiteral("请先生成有效的 100 单报告：%1\n%2").arg(path, QString::fromUtf8(error.what()));
        return demo;
    }
}

const DispatchDemoOrder* findDispatchOrder(const DispatchDemo& demo, const std::string& id) {
    for (const auto& order : demo.orders) if (order.job.id == id) return &order;
    return nullptr;
}
} // namespace cps::gui::merchant
