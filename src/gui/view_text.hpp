#pragma once
#include "print_controller.hpp"
#include <QDateTime>
#include <QJsonArray>
#include <algorithm>

// Display strings shared by the owner console and the student client.
namespace cps::gui::text {
inline QString state(JobState value) {
    switch (value) {
    case JobState::Queued: return QStringLiteral("等待中");
    case JobState::Running: return QStringLiteral("打印中");
    case JobState::Completed: return QStringLiteral("已完成");
    case JobState::Failed: return QStringLiteral("失败");
    case JobState::Cancelling: return QStringLiteral("取消中");
    case JobState::Cancelled: return QStringLiteral("已取消");
    }
    return {};
}
// Colour family for a state pill (see ui::tint).
inline const char* tone(JobState value) {
    switch (value) {
    case JobState::Running: return "info";
    case JobState::Completed: return "ready";
    case JobState::Failed: return "error";
    case JobState::Cancelling: return "waiting";
    case JobState::Queued:
    case JobState::Cancelled: break;
    }
    return "muted";
}
// `device` is one entry of the snapshot's fleet; it is empty in the legacy queue mode.
inline QString deviceName(const QJsonObject& device, int index) {
    return device.value("name").toString(QStringLiteral("打印机 %1").arg(index + 1, 2, 10, QLatin1Char('0')));
}
inline QString capability(const QJsonObject& device) {
    if (device.isEmpty()) return QStringLiteral("A4 黑白");
    return QString(device.value("a3").toBool() ? "A3/A4" : "A4") +
        (device.value("color").toBool() ? QStringLiteral(" 彩色/黑白") : QStringLiteral(" 黑白")) +
        (device.value("duplex").toBool() ? QStringLiteral(" 双面") : QStringLiteral(" 单面"));
}
inline QString money(Money cents) { return QStringLiteral("¥%1").arg(cents / 100.0, 0, 'f', 2); }
inline QString clock(TimeMs epochMs) { return QDateTime::fromMSecsSinceEpoch(epochMs).toString("HH:mm:ss"); }
inline QString duration(TimeMs ms) {
    const auto seconds = (std::max<TimeMs>(ms, 0) + 500) / 1000;
    if (seconds < 1) return QStringLiteral("不到 1 秒");
    if (seconds < 60) return QStringLiteral("约 %1 秒").arg(seconds);
    if (seconds < 3600) return QStringLiteral("约 %1 分 %2 秒").arg(seconds / 60).arg(seconds % 60);
    return QStringLiteral("约 %1 小时 %2 分").arg(seconds / 3600).arg(seconds % 3600 / 60);
}
inline QString startsIn(TimeMs wait) {
    return wait < 1000 ? QStringLiteral("即将开始") : duration(wait) + QStringLiteral("后开始");
}
inline QString afterResume(TimeMs wait) {
    return wait < 1000 ? QStringLiteral("恢复后立即开始") : QStringLiteral("恢复后再等") + duration(wait);
}
inline QString payment(PaymentMethod method) {
    switch (method) {
    case PaymentMethod::Counter: return QStringLiteral("柜台");
    case PaymentMethod::Wechat: return QStringLiteral("微信");
    case PaymentMethod::Alipay: return QStringLiteral("支付宝");
    case PaymentMethod::CampusCard: return QStringLiteral("校园卡");
    }
    return {};
}
inline QString blocked(const QString& reason) {
    if (reason == QLatin1String("no_paper")) return QStringLiteral("纸张不足，等待补货");
    if (reason == QLatin1String("no_device")) return QStringLiteral("兼容设备暂停接单");
    return QStringLiteral("排队较长，暂无法预计");
}
// The backend forecast assumes dispatch is running, so a paused queue is described
// by durations, which stay valid until dispatch resumes.
inline QString forecast(const JobView& job, TimeMs now, bool paused) {
    if (job.state != JobState::Queued && job.state != JobState::Running) return {};
    if (job.estimatedFinishMs < 0)
        return job.state == JobState::Queued && !job.blocked.isEmpty() ? blocked(job.blocked) : QString();
    if (job.state == JobState::Running) return QStringLiteral("预计 %1 完成").arg(clock(job.estimatedFinishMs));
    if (paused) return QStringLiteral("派发已暂停 · ") + afterResume(job.estimatedStartMs - now);
    return QStringLiteral("预计 %1 完成 · 前面 %2 单").arg(clock(job.estimatedFinishMs)).arg(job.ahead);
}
// Wording for a `quote` reply: what happens if the order were placed right now.
inline QString quoteForecast(const QJsonObject& quote) {
    if (quote.isEmpty()) return QStringLiteral("后台暂不可用，无法预计等待时间");
    if (quote.value("type").toString() != QLatin1String("quote")) return QStringLiteral("当前没有支持该规格的设备，无法下单");
    const auto reason = quote.value("estimate_blocked").toString();
    if (!quote.value("estimated_finish_ms").isDouble())
        return reason.isEmpty() ? QStringLiteral("暂无法预计等待时间") : blocked(reason);
    const auto wait = static_cast<TimeMs>(quote.value("estimated_wait_ms").toDouble());
    const auto total = static_cast<TimeMs>(quote.value("estimated_finish_ms").toDouble() - quote.value("now_ms").toDouble());
    if (quote.value("paused").toBool())
        return QStringLiteral("店铺暂停派发 · %1，%2完成").arg(afterResume(wait), duration(total));
    return QStringLiteral("前面 %1 单 · %2 · 预计 %3 完成").arg(quote.value("jobs_ahead").toInt())
        .arg(wait < 1000 ? QStringLiteral("可立即开始") : duration(wait) + QStringLiteral("后开始"),
             clock(static_cast<TimeMs>(quote.value("estimated_finish_ms").toDouble())));
}
inline QString pickupCode(const QString& jobId) {
    QString code;
    for (const auto c : jobId) if (c.isLetterOrNumber() && code.size() < 6) code += c.toUpper();
    return code;
}
} // namespace cps::gui::text
