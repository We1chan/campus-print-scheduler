#pragma once
#include "print_controller.hpp"
#include "cps/core/shop.hpp"
#include <QObject>
#include <QThread>
#include <QVector>
#include <QDateTime>
#include <QImage>
#include <array>
namespace cps::gui::merchant {
struct Order {
    QString id, title, student, service = QStringLiteral("普通"), state = QStringLiteral("待打印");
    int pages = 8, copies = 1, printed = 0, printer = -1;
    int estimatedMinutes = -1;
    bool color = false, duplex = true, a3 = false;
    qint64 amountCents = 0, costCents = 0, refundCents = 0;
    qint64 deadlineMs = 0, submittedMs = 0, finishedMs = 0;
    QString pickupTime, path, coverPath;
};
struct Device {
    int index = 0;
    QString name, jobId;
    bool online = true, enabled = true, busy = false, color = false, duplex = true, a3 = true;
    int printed = 0, total = 0, queued = 0, paper = 500, capacity = 500, todayPages = 0;
    std::array<int,4> toner{{62,76,54,68}};
};
struct Snapshot {
    QVector<Order> orders;
    QVector<Device> devices;
    int todayOrders = 0, waiting = 0, printing = 0, ready = 0, completed = 0;
    int printedPages = 0, blackPages = 0, colorPages = 0, clearMinutes = 0;
    qint64 revenueCents = 0;
    cps::ShopMetrics business;
    double averageMinutes = 0;
    std::array<int,3> serviceCounts{{0,0,0}};
    QVector<double> hourlyRevenue;
    QStringList hourlyLabels;
    QString date = QStringLiteral("2026年10月3日"), clock = QStringLiteral("10:24");
    QString connection;
    QString rangeLabel = QStringLiteral("今日"), rangeDate;
    bool connected = false, paused = false, showcase = false;
};
class MerchantData : public QObject {
    Q_OBJECT
public:
    explicit MerchantData(WindowOptions options, bool showcase, QObject* parent = nullptr);
    ~MerchantData() override;
    const Snapshot& snapshot() const { return snapshot_; }
    void start();
    void execute(const QString& action, const QString& value = {});
    void exportReport(const QString& path);
    void setDevice(int index, QJsonObject config);
    void addSampleDevice(const QString& name, bool color);
    void setRange(const QString& range);
    bool setOrderCover(const QString& id,const QImage& image);
signals:
    void changed();
    void feedback(const QString& text, bool problem);
    void backendReady();
private:
    void seedShowcase();
    void apply(const ViewState& state);
    void recount();
    WindowOptions options_;
    Snapshot snapshot_;
    PrintController* controller_ = nullptr;
    QThread worker_;
    QTimer* simulation_ = nullptr;
    ViewState lastState_;
    QString range_ = QStringLiteral("today");
    bool started_ = false;
};
}
