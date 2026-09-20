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

namespace cps::gui {
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
signals:
    void submitRequested(const QString& title, int pages, int copies, int priority);
    void importRequested(const QString& path);
    void cancelRequested(const QString& id);
    void policyRequested(int index);
    void pauseRequested(bool paused);
    void checkRequested();
    void exportRequested(const QString& path);
    void closeRequested();
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
    QLabel* metrics_ = nullptr;
    std::array<QLabel*, 2> printerLabels_{};
    std::array<QProgressBar*, 2> progressBars_{};
    QPushButton* cancel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    StatisticsChart* chart_ = nullptr;
    bool closeApproved_ = false;
    bool closePending_ = false;
};
} // namespace cps::gui
