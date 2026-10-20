#pragma once
#include "cps/core/scheduler.hpp"
#include "cps/metrics/statistics.hpp"
#include "event_stream.hpp"
#include <QLocalSocket>
#include <QJsonObject>

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
    bool shopMode = false;
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
    QString specification = {};
    Money quotedCents = 0;
    QString deadline = {};
    TimeMs estimatedStartMs = -1;  // backend forecast, -1 when there is none
    TimeMs estimatedFinishMs = -1;
    QString blocked = {};          // no_device / no_paper / horizon
    int ahead = 0;
    QString customer = {};
    PaymentMethod payment = PaymentMethod::Counter;
    Money refundCents = 0;
    int printer = -1;
};
struct PrinterView {
    QString jobId;
    QString title;
    QString status = QStringLiteral("空闲");
    int printed = 0;
    int total = 0;
    QString connection;
    TimeMs estimatedFinishMs = -1;
    QString customer = {};
};
struct ViewState {
    QList<JobView> jobs;
    QStringList waitingOrder; // Snapshot order under the active policy, including aging.
    int policyIndex = 0;
    std::vector<PrinterView> printers = std::vector<PrinterView>(2);
    QJsonObject shop;
    Statistics metrics;
    TimeMs nowMs = 0;        // backend clock of this snapshot
    TimeMs forecastAtMs = 0; // reference clock for the cached forecast's paused durations
    TimeMs queueClearMs = -1;
    QString connection;
    bool paused = false;
    bool connected = false;
    bool demoAvailable = false;
};

// IPC runs in a worker thread. The coordinator owns all queue and process state.
class PrintController : public QObject {
    Q_OBJECT
public:
    explicit PrintController(WindowOptions options);
public slots:
    void initialize();
    void submit(const QString& title, int pages, int copies, int priority);
    void importFile(const QString& path);
    void submitOrder(QJsonObject order);
    void submitDemoBatch();
    void clearDemoOrders();
    void requestQuote(QJsonObject order, int token);
    void runExperiments(const QString& path, int customers);
    void shopCommand(const QString& type, QJsonObject payload);
    void cancel(const QString& id);
    void setPolicy(int index);
    void setPaused(bool paused);
    void checkConnection();
    void exportReport(const QString& path);
    void requestClose();
    void injectFault(const QString& id);
    void disconnectControl(const QString& id);
signals:
    void experimentReady(const QJsonObject& report);
    // An empty quote means the backend was unreachable; type "error" means rejected.
    void quoteReady(int token, const QJsonObject& quote);
    void orderResult(bool accepted, const QString& jobId, const QString& message);
    void viewChanged(const cps::gui::ViewState& state);
    // `problem` marks a rejected, failed or unconfirmed action so the window can colour it.
    void feedback(const QString& text, bool problem = false);
    void logsReady(const QStringList& lines);
    void closeReady(bool allowed);
private:
    bool connectBackend();
    QJsonObject request(const QString& type, QJsonObject payload = {});
    QJsonObject mutate(const QString& type, QJsonObject payload = {});
    void refresh();
    void publish();
    void disconnected();
    void log(const QString& line);
    bool applySnapshot(const QJsonObject& snapshot);
    WindowOptions options_;
    QLocalSocket* socket_ = nullptr;
    QProcess* experiments_ = nullptr;
    QTimer* refresh_ = nullptr;
    qint64 lastLaunchMs_ = 0;
    bool available_ = false;
    bool demoAvailable_ = false;
    ViewState view_;
    QJsonObject snapshot_;
    QStringList logs_;

};
} // namespace cps::gui
Q_DECLARE_METATYPE(cps::gui::ViewState)
