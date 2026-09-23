#pragma once
#include "cps/core/scheduler.hpp"
#include "cps/metrics/statistics.hpp"
#include "event_stream.hpp"
#include "control_client.hpp"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <array>
#include <memory>

namespace cps::gui {
struct WindowOptions {
    QString stateDirectory;
    int pageMs = 150;
    bool demoMode = false;
};
struct JobView {
    QString id;
    QString title;
    int sheets = 0;
    int priority = 0;
    JobState state = JobState::Queued;
    int printed = 0;
    QString detail;
    bool controlReady = false;
};
struct PrinterView {
    QString jobId;
    QString title;
    QString status = QStringLiteral("空闲");
    int printed = 0;
    int total = 0;
    QString connection;
};
struct ViewState {
    QList<JobView> jobs;
    QStringList waitingOrder; // Snapshot order under the active policy, including aging.
    int policyIndex = 0;
    std::array<PrinterView, 2> printers;
    Statistics metrics;
    QString connection;
    bool paused = false;
};

// This object, its timers, scheduler and QProcesses live in one worker thread.
// Only value snapshots and text cross to the QWidget thread via queued signals.
class PrintController : public QObject {
    Q_OBJECT
public:
    explicit PrintController(WindowOptions options);
public slots:
    void initialize();
    void submit(const QString& title, int pages, int copies, int priority);
    void importFile(const QString& path);
    void cancel(const QString& id);
    void setPolicy(int index);
    void setPaused(bool paused);
    void checkConnection();
    void exportReport(const QString& path);
    void requestClose();
    void injectFault(const QString& id);
    void disconnectControl(const QString& id);
signals:
    void viewChanged(const cps::gui::ViewState& state);
    void feedback(const QString& text);
    void logsReady(const QStringList& lines);
    void closeReady(bool allowed);
private:
    struct Run {
        Run(const Job& claimed, QProcess* child) : job(claimed), process(child),
            stream(QString::fromStdString(claimed.id)) {}
        Job job;
        QProcess* process;
        EventStream stream;
        bool outputCommitted = false;
        bool supervisorCompleted = false;
        QString detail;
        ControlClient* control = nullptr;
        QString controlToken;
        bool cancelRequested = false;
        bool cancelledConfirmed = false;
    };
    bool addJob(const QString& title, int pages, int copies, int priority);
    void dispatch();
    void receive(int slot, const EventBatch& batch);
    void complete(int slot, int exitCode, QProcess::ExitStatus status);
    void publish();
    void log(const QString& line);
    WindowOptions options_;
    Scheduler scheduler_;
    TimeMs startedMs_ = 0;
    Policy policy_ = Policy::Fcfs;
    bool paused_ = false;
    bool available_ = false;
    QHash<QString, JobView> details_;
    std::array<std::unique_ptr<Run>, 2> runs_;
    std::array<PrinterView, 2> printers_;
    QStringList logs_;
};
} // namespace cps::gui
Q_DECLARE_METATYPE(cps::gui::ViewState)
