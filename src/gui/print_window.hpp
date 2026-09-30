#pragma once
#include "print_controller.hpp"
#include <QAbstractTableModel>
#include <QMainWindow>
#include <QThread>
#include <QWidget>
#include <array>
class QLabel;
class QPushButton;
class QProgressBar;
class QTableView;
class QSortFilterProxyModel;
class QPlainTextEdit;
class QToolButton;
class QTableWidget;

namespace cps::gui {
class PaperWorkspace;
class PrinterTimeline;
class JobTableModel : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void update(const QList<JobView>& jobs);
private:
    QList<JobView> jobs_;
};
class StatisticsChart : public QWidget {
public:
    explicit StatisticsChart(QWidget* parent = nullptr);
    void updateMetrics(const Statistics& metrics);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    Statistics metrics_;
};
class PrintWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PrintWindow(WindowOptions options, QWidget* parent = nullptr);
    ~PrintWindow() override;
    void importFile(const QString& path);
    void loadDemo();
signals:
    void backendReady();
    void submitRequested(const QString& title, int pages, int copies, int priority);
    void orderRequested(QJsonObject order);
    void quoteRequested(QJsonObject order, int token);
    void experimentRequested(const QString& path, int customers);
    void shopRequested(const QString& type, QJsonObject payload);
    void importRequested(const QString& path);
    void cancelRequested(const QString& id);
    void policyRequested(int index);
    void pauseRequested(bool paused);
    void checkRequested();
    void exportRequested(const QString& path);
    void closeRequested();
    void faultRequested(const QString& id);
    void disconnectRequested(const QString& id);
protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void applyView(const ViewState& view);
    void updateSelection();
    void updateDeviceActions();
    void fitTitleColumn();
    QThread backendThread_;
    PrintController* controller_ = nullptr;
    JobTableModel* model_ = nullptr;
    QSortFilterProxyModel* filter_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* feedback_ = nullptr;
    QLabel* connection_ = nullptr;
    PaperWorkspace* scene_ = nullptr;
    PrinterTimeline* timeline_ = nullptr;
    std::array<QLabel*, 4> metricValues_{};
    std::array<QLabel*, 4> metricHints_{};
    QTableWidget* fleetTable_ = nullptr;
    QLabel* fleetSummary_ = nullptr;
    QPushButton* toggleDevice_ = nullptr;
    QPushButton* editDevice_ = nullptr;
    QLabel* shopSummary_ = nullptr;
    QLabel* shopForecast_ = nullptr;
    std::array<QLabel*, 4> shopValues_{};
    std::array<QLabel*, 4> shopHints_{};
    std::array<QLabel*, 12> ledger_{}; // money, then takings per channel, then fulfilment counts
    std::array<QLabel*, 2> stock_{};   // A4, A3
    int quoteToken_ = 0;
    QJsonObject shopSnapshot_;
    QLabel* queueSummary_ = nullptr;
    QWidget* emptyState_ = nullptr;
    QToolButton* logToggle_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* refund_ = nullptr;
    QPushButton* fault_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    StatisticsChart* chart_ = nullptr;
    bool closeApproved_ = false;
    bool demoAvailable_ = false;
    bool closePending_ = false;
    bool shopReady_ = false;  // connected to a coordinator that runs the shop mode
};
} // namespace cps::gui
