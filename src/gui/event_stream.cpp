#include "event_stream.hpp"
#include <QJsonDocument>
#include <cmath>

namespace cps::gui {
namespace {
bool integer(const QJsonValue& value, double minimum, double maximum) {
    const double n = value.toDouble(-1);
    return value.isDouble() && n >= minimum && n <= maximum && std::floor(n) == n;
}
}

void EventStream::parse(const QByteArray& line, EventBatch& result) const {
    if (line.trimmed().isEmpty()) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(line, &error);
    const auto event = document.object();
    const auto type = event.value("type").toString();
    bool valid = error.error == QJsonParseError::NoError && document.isObject() &&
        integer(event.value("schema_version"), 1, 1) &&
        event.value("job_id").toString() == jobId_;
    if (type == "progress") {
        valid = valid && integer(event.value("total_sheets"), 1, 10000) &&
            integer(event.value("printed_sheets"), 0, event.value("total_sheets").toDouble(-1));
    } else if (type == "worker_started") {
        valid = valid && integer(event.value("pid"), 1, 9007199254740991.0) &&
            integer(event.value("attempt"), 1, 11);
    } else if (type == "worker_restarting") {
        valid = valid && integer(event.value("restart"), 1, 10) && event.value("exit_code").isDouble();
    } else if (type == "completed") {
        valid = valid && !event.value("output_path").toString().isEmpty();
    } else if (type == "supervisor_completed") {
        valid = valid && integer(event.value("restarts"), 0, 10);
    } else if (type == "failed") {
        valid = valid && (event.value("reason").isString() || event.value("exit_code").isDouble());
    } else if (type != "worker_timeout") {
        valid = false;
    }
    if (valid) result.events.append(event);
    else result.errors.append(QStringLiteral("忽略无效事件：JSON、版本、任务 ID 或字段不符合 v1 协议。"));
}

EventBatch EventStream::append(const QByteArray& bytes) {
    EventBatch result;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const auto newline = bytes.indexOf('\n', offset);
        const auto length = (newline < 0 ? bytes.size() : newline) - offset;
        if (!discarding_) {
            if (pending_.size() + length > MaxFrameBytes) {
                pending_.clear();
                discarding_ = true;
                result.errors.append(QStringLiteral("事件超过 64 KiB，已丢弃至下一个换行符。"));
            } else {
                pending_.append(bytes.constData() + offset, length);
            }
        }
        if (newline < 0) break;
        if (!discarding_) parse(pending_, result);
        pending_.clear();
        discarding_ = false;
        offset = newline + 1;
    }
    return result;
}

EventBatch EventStream::finish() {
    EventBatch result;
    if (!pending_.isEmpty() || discarding_)
        result.errors.append(QStringLiteral("连接关闭时仍有未完成的 JSONL 消息。"));
    pending_.clear();
    discarding_ = false;
    return result;
}
} // namespace cps::gui
