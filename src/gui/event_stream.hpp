#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>

namespace cps::gui {
struct EventBatch {
    QList<QJsonObject> events;
    QStringList errors;
};

// One instance per QProcess stream. Frame bytes before decoding UTF-8.
class EventStream {
public:
    static constexpr qsizetype MaxFrameBytes = 64 * 1024;
    explicit EventStream(QString jobId) : jobId_(std::move(jobId)) {}
    EventBatch append(const QByteArray& bytes);
    EventBatch finish();
private:
    void parse(const QByteArray& line, EventBatch& result) const;
    QString jobId_;
    QByteArray pending_;
    bool discarding_ = false;
};
} // namespace cps::gui
