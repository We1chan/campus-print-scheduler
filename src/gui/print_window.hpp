#pragma once
#include "print_controller.hpp"
#include <QAbstractTableModel>
#include <QMainWindow>
#include <QThread>
#include <QWidget>
class QLabel;
class QPushButton;
class QProgressBar;
class QTableView;
class QSortFilterProxyModel;
class QPlainTextEdit;
class QToolButton;

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
    void submitRequested(const QString& title, int pages, int copies, int priority);
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
private:
    void applyView(const ViewState& view);
    void updateSelection();
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
    std::array<QLabel*, 2> printerLabels_{};
    std::array<QLabel*, 2> printerStates_{};
    std::array<QLabel*, 2> printerDetails_{};
    std::array<QLabel*, 2> printerProgress_{};
    std::array<QProgressBar*, 2> progressBars_{};
    QLabel* queueSummary_ = nullptr;
    QWidget* emptyState_ = nullptr;
    QToolButton* logToggle_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* fault_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    StatisticsChart* chart_ = nullptr;
    bool closeApproved_ = false;
    bool closePending_ = false;
};
} // namespace cps::gui
