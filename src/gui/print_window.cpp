#include "print_window.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace cps::gui {
namespace {
QString stateName(JobState state) {
    switch (state) {
    case JobState::Queued: return QStringLiteral("等待中");
    case JobState::Running: return QStringLiteral("打印中");
    case JobState::Completed: return QStringLiteral("已完成");
    case JobState::Failed: return QStringLiteral("失败");
    case JobState::Cancelling: return QStringLiteral("取消中");
    case JobState::Cancelled: return QStringLiteral("已取消");
    }
    return {};
}
}
int JobTableModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(jobs_.size()); }
int JobTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 7; }
QVariant JobTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= jobs_.size()) return {};
    const auto& job = jobs_[index.row()];
    if (role == Qt::UserRole) return job.id;
    if (role == Qt::UserRole + 1) return static_cast<int>(job.state);
    if (role == Qt::UserRole + 2) return job.controlReady;
    if (role == Qt::ToolTipRole) return job.detail.isEmpty() ? job.id : job.detail;
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case 0: return job.title;
    case 1: return job.sheets;
    case 2: return job.priority;
    case 3: return stateName(job.state);
    case 4: return QStringLiteral("%1 / %2").arg(job.printed).arg(job.sheets);
    case 5: return job.detail;
    case 6: return job.id;
    default: return {};
    }
}
QVariant JobTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    const QStringList headings{QStringLiteral("文档"), QStringLiteral("总页数"), QStringLiteral("优先级"),
        QStringLiteral("状态"), QStringLiteral("已打印 / 总页数"), QStringLiteral("结果 / 错误"), QStringLiteral("任务 ID")};
    return headings.value(section);
}
void JobTableModel::update(const QList<JobView>& jobs) {
    // Stable insertion order preserves selection; no per-cell QWidget allocation.
    const auto oldSize = jobs_.size();
    if (jobs.size() < oldSize) {
        beginResetModel(); jobs_ = jobs; endResetModel(); return;
    }
    for (qsizetype row = 0; row < oldSize; ++row) {
        const auto& next = jobs[row];
        auto& current = jobs_[row];
        if (current.state != next.state || current.printed != next.printed || current.detail != next.detail || current.controlReady != next.controlReady) {
            current = next;
            emit dataChanged(index(static_cast<int>(row), 3), index(static_cast<int>(row), 5));
        }
    }
    if (jobs.size() > oldSize) {
        beginInsertRows({}, static_cast<int>(oldSize), static_cast<int>(jobs.size()) - 1);
        for (qsizetype row = oldSize; row < jobs.size(); ++row) jobs_.append(jobs[row]);
        endInsertRows();
    }
}
StatisticsChart::StatisticsChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(118);
    setAccessibleName(QStringLiteral("任务状态统计图"));
}
void StatisticsChart::updateMetrics(const Statistics& metrics) { metrics_ = metrics; update(); }
void StatisticsChart::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const std::array<std::size_t, 5> values{metrics_.queued, metrics_.running + metrics_.cancelling,
        metrics_.completed, metrics_.failed, metrics_.cancelled};
    const QStringList labels{QStringLiteral("等待"), QStringLiteral("运行"), QStringLiteral("完成"), QStringLiteral("失败"), QStringLiteral("取消")};
    const std::array<QColor, 5> colors{QColor("#94a3b8"), QColor("#2563eb"), QColor("#16a34a"), QColor("#dc2626"), QColor("#a1a1aa")};
    const double maximum = static_cast<double>(std::max<std::size_t>(1, *std::max_element(values.begin(), values.end())));
    const int band = width() / 5;
    for (int i = 0; i < 5; ++i) {
        const int barHeight = static_cast<int>(static_cast<double>(values[i]) / maximum * (height() - 48));
        painter.fillRect(QRect(i * band + band / 4, height() - 25 - barHeight, band / 2, barHeight), colors[i]);
        painter.setPen(palette().text().color());
        painter.drawText(QRect(i * band, height() - 47 - barHeight, band, 20), Qt::AlignCenter,
            QString::number(static_cast<qulonglong>(values[i])));
        painter.drawText(QRect(i * band, height() - 22, band, 22), Qt::AlignCenter, labels[i]);
    }
}
PrintWindow::PrintWindow(WindowOptions options, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("校园共享打印调度"));
    resize(1180, 900);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setSpacing(10);
    layout->setContentsMargins(20, 16, 20, 16);
    auto* heading = new QLabel(QStringLiteral("校园共享打印调度"));
    heading->setStyleSheet("font-size: 25px; font-weight: 600;");
    layout->addWidget(heading);
    auto* note = new QLabel(QStringLiteral("两台模拟打印机 · 关闭应用前请完成或取消等待任务。队列暂不支持应用重启恢复。"));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* top = new QHBoxLayout;
    auto* submission = new QGroupBox(QStringLiteral("提交任务"));
    auto* form = new QFormLayout(submission);
    auto* title = new QLineEdit(QStringLiteral("操作系统课设文档"));
    title->setObjectName("jobTitle"); title->setMaxLength(200);
    auto* pages = new QSpinBox;
    pages->setObjectName("jobPages"); pages->setRange(1, 1000); pages->setValue(8);
    auto* copies = new QSpinBox; copies->setRange(1, 100);
    auto* priority = new QSpinBox; priority->setRange(0, 100);
    priority->setToolTip(QStringLiteral("数值越大越优先；等待每 30 秒增加 1 个有效优先级。"));
    form->addRow(QStringLiteral("文档名称"), title);
    auto* numbers = new QHBoxLayout;
    numbers->addWidget(new QLabel(QStringLiteral("页数"))); numbers->addWidget(pages);
    numbers->addWidget(new QLabel(QStringLiteral("份数"))); numbers->addWidget(copies);
    numbers->addWidget(new QLabel(QStringLiteral("优先级"))); numbers->addWidget(priority);
    form->addRow(numbers);
    auto* policy = new QComboBox;
    policy->addItems({QStringLiteral("先来先服务 FCFS"), QStringLiteral("短作业优先 SJF"), QStringLiteral("优先级 + 等待老化")});
    policy->setObjectName("schedulingPolicy");
    form->addRow(QStringLiteral("等待任务排序"), policy);
    auto* buttons = new QHBoxLayout;
    auto* submit = new QPushButton(QStringLiteral("提交模拟打印")); submit->setObjectName("submitJob");
    auto* import = new QPushButton(QStringLiteral("批量导入 JSON"));
    buttons->addWidget(submit); buttons->addWidget(import); form->addRow(buttons);
    top->addWidget(submission, 3);
    auto* statistics = new QGroupBox(QStringLiteral("本次会话统计"));
    auto* statisticsLayout = new QVBoxLayout(statistics);
    metrics_ = new QLabel; metrics_->setWordWrap(true); statisticsLayout->addWidget(metrics_);
    chart_ = new StatisticsChart; statisticsLayout->addWidget(chart_);
    top->addWidget(statistics, 2); layout->addLayout(top);
    auto* printers = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        auto* box = new QGroupBox(QStringLiteral("打印机 %1").arg(i + 1));
        auto* printerLayout = new QVBoxLayout(box);
        printerLabels_[i] = new QLabel(QStringLiteral("空闲"));
        printerLabels_[i]->setWordWrap(true); printerLabels_[i]->setMinimumHeight(40);
        printerLayout->addWidget(printerLabels_[i]);
        progressBars_[i] = new QProgressBar;
        progressBars_[i]->setRange(0, 100); progressBars_[i]->setValue(0);
        printerLayout->addWidget(progressBars_[i]); printers->addWidget(box);
    }
    layout->addLayout(printers);
    auto* actions = new QHBoxLayout;
    auto* filter = new QComboBox;
    filter->addItems({QStringLiteral("全部任务"), QStringLiteral("等待中"), QStringLiteral("打印中"),
        QStringLiteral("已完成"), QStringLiteral("失败"), QStringLiteral("已取消")});
    actions->addWidget(filter);
    auto* pause = new QPushButton(QStringLiteral("暂停派发"));
    pause->setObjectName("pauseDispatch"); pause->setCheckable(true);
    pause->setToolTip(QStringLiteral("只暂停领取新任务；已运行的任务继续打印。")); actions->addWidget(pause);
    cancel_ = new QPushButton(QStringLiteral("取消选中任务"));
    cancel_->setObjectName("cancelJob"); cancel_->setEnabled(false); actions->addWidget(cancel_);
    auto* report = new QPushButton(QStringLiteral("导出统计报表")); actions->addWidget(report); actions->addStretch();
    if (options.demoMode) {
        fault_ = new QPushButton(QStringLiteral("演示：工作进程崩溃"));
        fault_->setObjectName("injectFault"); fault_->setEnabled(false);
        fault_->setToolTip(QStringLiteral("结束选中任务的真实 worker，观察监督进程有限重试。"));
        actions->addWidget(fault_);
        disconnect_ = new QPushButton(QStringLiteral("演示：控制连接断开"));
        disconnect_->setObjectName("disconnectControl"); disconnect_->setEnabled(false);
        actions->addWidget(disconnect_);
    }
    layout->addLayout(actions);
    auto* splitter = new QSplitter(Qt::Vertical);
    model_ = new JobTableModel(this);
    filter_ = new QSortFilterProxyModel(this); filter_->setSourceModel(model_); filter_->setFilterKeyColumn(3);
    table_ = new QTableView; table_->setObjectName("jobsTable"); table_->setModel(filter_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false); table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->setColumnWidth(0, 220); table_->setColumnWidth(1, 65); table_->setColumnWidth(2, 65);
    table_->setColumnWidth(3, 85); table_->setColumnWidth(4, 125); table_->setColumnWidth(5, 240); table_->setColumnWidth(6, 290);
    splitter->addWidget(table_);
    log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->setMaximumBlockCount(500);
    log_->setPlaceholderText(QStringLiteral("执行事件与错误日志")); splitter->addWidget(log_);
    splitter->setStretchFactor(0, 4); splitter->setStretchFactor(1, 1); layout->addWidget(splitter, 1);
    feedback_ = new QLabel(QStringLiteral("可提交任务，或先暂停派发再批量导入。"));
    feedback_->setObjectName("feedback"); feedback_->setWordWrap(true); layout->addWidget(feedback_);
    auto* connectionLayout = new QHBoxLayout;
    connection_ = new QLabel(QStringLiteral("正在检查本地执行器…")); connectionLayout->addWidget(connection_);
    auto* check = new QPushButton(QStringLiteral("重新检查执行器")); connectionLayout->addWidget(check);
    connectionLayout->addStretch(); layout->addLayout(connectionLayout);
    layout->addWidget(new QLabel(options.demoMode ? QStringLiteral("演示模式：可注入真实 worker 故障和控制连接断开。") :
        QStringLiteral("运行中取消会等待进程停止确认；控制连接中断会自动重连。")));
    auto* location = new QLabel(QStringLiteral("运行数据目录：") + options.stateDirectory);
    location->setWordWrap(true); location->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(location);
    setCentralWidget(central);
    qRegisterMetaType<ViewState>();
    controller_ = new PrintController(options); controller_->moveToThread(&backendThread_);
    connect(&backendThread_, &QThread::started, controller_, &PrintController::initialize);
    connect(&backendThread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(this, &PrintWindow::submitRequested, controller_, &PrintController::submit, Qt::QueuedConnection);
    connect(this, &PrintWindow::importRequested, controller_, &PrintController::importFile, Qt::QueuedConnection);
    connect(this, &PrintWindow::cancelRequested, controller_, &PrintController::cancel, Qt::QueuedConnection);
    connect(this, &PrintWindow::policyRequested, controller_, &PrintController::setPolicy, Qt::QueuedConnection);
    connect(this, &PrintWindow::pauseRequested, controller_, &PrintController::setPaused, Qt::QueuedConnection);
    connect(this, &PrintWindow::checkRequested, controller_, &PrintController::checkConnection, Qt::QueuedConnection);
    connect(this, &PrintWindow::exportRequested, controller_, &PrintController::exportReport, Qt::QueuedConnection);
    connect(this, &PrintWindow::closeRequested, controller_, &PrintController::requestClose, Qt::QueuedConnection);
    connect(this, &PrintWindow::faultRequested, controller_, &PrintController::injectFault, Qt::QueuedConnection);
    connect(this, &PrintWindow::disconnectRequested, controller_, &PrintController::disconnectControl, Qt::QueuedConnection);
    if (fault_) connect(fault_, &QPushButton::clicked, this, [this] {
        emit faultRequested(table_->currentIndex().data(Qt::UserRole).toString());
    });
    if (disconnect_) connect(disconnect_, &QPushButton::clicked, this, [this] {
        emit disconnectRequested(table_->currentIndex().data(Qt::UserRole).toString());
    });
    connect(controller_, &PrintController::closeReady, this, [this](bool allowed) {
        closePending_ = false; centralWidget()->setEnabled(true);
        if (allowed) { closeApproved_ = true; close(); }
    }, Qt::QueuedConnection);
    connect(controller_, &PrintController::viewChanged, this, &PrintWindow::applyView, Qt::QueuedConnection);
    connect(controller_, &PrintController::feedback, feedback_, &QLabel::setText, Qt::QueuedConnection);
    connect(controller_, &PrintController::logsReady, this, [this](const QStringList& lines) {
        log_->appendPlainText(lines.join('\n'));
    }, Qt::QueuedConnection);
    connect(submit, &QPushButton::clicked, this, [=] {
        emit submitRequested(title->text(), pages->value(), copies->value(), priority->value());
    });
    connect(import, &QPushButton::clicked, this, [this, options] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入模拟任务数组"), options.stateDirectory, "JSON (*.json)");
        if (!path.isEmpty()) importFile(path);
    });
    connect(policy, &QComboBox::currentIndexChanged, this, &PrintWindow::policyRequested);
    connect(pause, &QPushButton::toggled, this, [this, pause](bool paused) {
        pause->setText(paused ? QStringLiteral("继续派发") : QStringLiteral("暂停派发")); emit pauseRequested(paused);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { emit cancelRequested(table_->currentIndex().data(Qt::UserRole).toString()); });
    connect(check, &QPushButton::clicked, this, &PrintWindow::checkRequested);
    connect(report, &QPushButton::clicked, this, [this, options] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出会话统计"), options.stateDirectory + "/report.json", "JSON (*.json)");
        if (!path.isEmpty()) emit exportRequested(path);
    });
    connect(filter, &QComboBox::currentIndexChanged, this, [this, filter](int index) {
        filter_->setFilterFixedString(index == 0 ? QString() : filter->currentText()); updateSelection();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PrintWindow::updateSelection);
    backendThread_.start();
}
PrintWindow::~PrintWindow() { backendThread_.quit(); backendThread_.wait(); }
void PrintWindow::importFile(const QString& path) { emit importRequested(path); }
void PrintWindow::applyView(const ViewState& view) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    model_->update(view.jobs);
    connection_->setText(view.connection + (view.paused ? QStringLiteral(" · 已暂停派发") : QString()));
    for (int i = 0; i < 2; ++i) {
        const auto& printer = view.printers[i];
        printerLabels_[i]->setText(printer.title.isEmpty() ? printer.status : printer.title.left(55) + "\n" + printer.status + " · " + printer.connection);
        progressBars_[i]->setRange(0, printer.total > 0 ? printer.total : 100);
        progressBars_[i]->setValue(printer.printed);
        progressBars_[i]->setFormat(printer.total > 0 ? QStringLiteral("%v / %m 页（%p%）") : QStringLiteral("空闲"));
    }
    const auto& m = view.metrics;
    metrics_->setText(QStringLiteral("首次平均等待 %1 秒 · 成功平均周转 %2 秒\n吞吐量 %3 任务/分钟 · 设备利用率 %4%")
        .arg(m.averageFirstWaitMs / 1000, 0, 'f', 2).arg(m.averageTurnaroundMs / 1000, 0, 'f', 2)
        .arg(m.throughputPerSecond * 60, 0, 'f', 2).arg(m.utilization * 100, 0, 'f', 1));
    chart_->updateMetrics(m); updateSelection();
}
void PrintWindow::updateSelection() {
    const auto current = table_->currentIndex();
    const bool queued = current.isValid() && current.data(Qt::UserRole + 1).toInt() == static_cast<int>(JobState::Queued);
    const bool running = current.isValid() && current.data(Qt::UserRole + 1).toInt() == static_cast<int>(JobState::Running);
    const bool connected = current.data(Qt::UserRole + 2).toBool();
    cancel_->setEnabled(queued || (running && connected));
    cancel_->setToolTip(queued ? QStringLiteral("立即取消尚未领取的任务") : QStringLiteral("请求停止正在执行的任务，收到停止确认后生效。"));
    if (fault_) fault_->setEnabled(running && connected);
    if (disconnect_) disconnect_->setEnabled(running && connected);
}
void PrintWindow::closeEvent(QCloseEvent* event) {
    if (closeApproved_) { event->accept(); return; }
    event->ignore();
    if (closePending_) return;
    closePending_ = true; centralWidget()->setEnabled(false); emit closeRequested();
}
} // namespace cps::gui
