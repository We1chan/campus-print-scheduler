#include "print_window.hpp"
#include "ui_theme.hpp"
#include "studio_shell.hpp"
#include "paper_workspace.hpp"
#include "view_text.hpp"
#include "cps/runtime/job_io.hpp"
#include "cps/runtime/shop_io.hpp"
#include <QProcess>
#include <QCheckBox>
#include <QTableWidget>
#include <QInputDialog>
#include <QJsonArray>
#include <QUuid>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QScreen>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>
#include <QToolButton>
#include <QUrl>
#include <QDialog>
#include <QListWidget>
#include <QShortcut>
#include <QRegularExpression>
#include <algorithm>
#include <functional>
#include <utility>

namespace cps::gui {
namespace {
using ui::label;
using ui::card;
using ui::tone;

using SelectInput = ui::SelectInput;
using NumberInput = ui::NumberInput;

// Draw the visible cells only, retaining the existing model and selection semantics.
class QueueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        painter->fillRect(option.rect, QColor(selected ? "#EEEEFC" : hovered ? "#F6F8FC" : "#FFFFFF"));
        painter->setPen(QColor("#E7ECF3"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect area = option.rect.adjusted(10, 0, -10, 0);
        QFont font = option.font;
        font.setPixelSize(12);
        painter->setFont(font);
        if (index.column() == 0) {
            ui::icon(ui::Icon::Document, QColor("#7A8FAE")).paint(painter, QRect(area.left(), area.center().y() - 11, 22, 22));
            font.setPixelSize(13);
            font.setWeight(QFont::Medium);
            painter->setFont(font);
            painter->setPen(QColor("#293449"));
            const QRect name(area.left() + 32, area.top() + 9, std::max(0, area.width() - 32), 21);
            painter->drawText(name, Qt::AlignVCenter, painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, name.width()));
            font.setPixelSize(10); font.setWeight(QFont::Normal); painter->setFont(font);
            painter->setPen(QColor("#8A96A9"));
            painter->drawText(name.translated(0, 20), Qt::AlignVCenter,
                painter->fontMetrics().elidedText(index.data(Qt::UserRole + 7).toString(), Qt::ElideRight, name.width()));
        } else if (index.column() == 3) {
            const auto colors = ui::tint(QLatin1String(text::tone(static_cast<JobState>(index.data(Qt::UserRole + 1).toInt()))));
            const QString text = index.data().toString();
            const int width = std::min(area.width(), painter->fontMetrics().horizontalAdvance(text) + 18);
            painter->setPen(Qt::NoPen); painter->setBrush(colors.fill);
            const QRect pill(area.left(), area.center().y() - 12, width, 24);
            painter->drawRoundedRect(pill, 6, 6); painter->setPen(colors.ink);
            painter->drawText(pill, Qt::AlignCenter, text);
        } else if (index.column() == 4) {
            const int printed = index.data(Qt::UserRole + 3).toInt();
            const int sheets = index.data(Qt::UserRole + 4).toInt();
            painter->setPen(QColor("#62687D"));
            painter->drawText(QRect(area.left(), area.top() + 10, area.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QStringLiteral("%1 / %2 页").arg(printed).arg(sheets));
            const QRectF track(area.left(), area.bottom() - 15, area.width(), 4);
            painter->setPen(Qt::NoPen); painter->setBrush(QColor("#E4E9F2")); painter->drawRoundedRect(track, 2, 2);
            if (sheets > 0 && printed > 0) {
                painter->setBrush(QColor(printed >= sheets ? "#4548E8" : "#4548E8"));
                painter->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * printed / sheets, 4), 2, 2);
            }
        } else {
            painter->setPen(QColor(index.column() == 5 ? "#62687D" : "#59667C"));
            const auto value = painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, area.width());
            painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, value);
        }
        painter->restore();
    }
};
}
int JobTableModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(jobs_.size()); }
int JobTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 11; }
QVariant JobTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= jobs_.size()) return {};
    const auto& job = jobs_[index.row()];
    if (role == Qt::UserRole) return job.id;
    if (role == Qt::UserRole + 1) return static_cast<int>(job.state);
    if (role == Qt::UserRole + 2) return job.controlReady;
    if (role == Qt::UserRole + 3) return job.printed;
    if (role == Qt::UserRole + 4) return job.sheets;
    if (role == Qt::UserRole + 5) return double(job.quotedCents);
    if (role == Qt::UserRole + 6) return double(job.refundCents);
    // Second line of the document cell: what the counter needs to hand the order over.
    if (role == Qt::UserRole + 7) return job.customer.isEmpty() ? QStringLiteral("# ") + job.id.left(8)
        : QStringLiteral("学号 %1 · 取件码 %2").arg(job.customer, text::pickupCode(job.id));
    if (role == Qt::ToolTipRole) return job.title + "\n" + job.id + (job.detail.isEmpty() ? QString() : "\n" + job.detail) +
        (job.customer.isEmpty() ? QString() : QStringLiteral("\n学生 ") + job.customer) +
        (job.payment == PaymentMethod::Counter ? QString() : QStringLiteral("\n在线支付 · ") + text::payment(job.payment) + QStringLiteral("（模拟）"));
    if (role != Qt::DisplayRole) return {};
    switch (index.column()) {
    case 0: return job.title;
    case 1: return job.sheets;
    case 2: return job.priority;
    case 3: return text::state(job.state);
    case 4: return QStringLiteral("%1 / %2").arg(job.printed).arg(job.sheets);
    case 5: return job.detail;
    case 6: return job.id;
    case 7: return job.specification;
    case 8: return text::money(job.quotedCents) +
        (job.payment == PaymentMethod::Counter ? QString() : QStringLiteral(" · ") + text::payment(job.payment)) +
        (job.refundCents > 0 ? QStringLiteral(" · 已退 ") + text::money(job.refundCents) : QString());
    case 9: return job.deadline;
    // Hidden column the search box filters on.
    case 10: return job.title + QLatin1Char(' ') + job.id + QLatin1Char(' ') + text::pickupCode(job.id) + QLatin1Char(' ') + job.customer;
    default: return {};
    }
}
QVariant JobTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    const QStringList headings{QStringLiteral("文档"), QStringLiteral("总页数"), QStringLiteral("优先级"),
        QStringLiteral("状态"), QStringLiteral("已打印 / 总页数"), QStringLiteral("预计 / 结果"), QStringLiteral("任务 ID"), QStringLiteral("规格"), QStringLiteral("锁定报价"), QStringLiteral("交付期限")};
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
        if (current.state != next.state || current.printed != next.printed || current.detail != next.detail ||
            current.controlReady != next.controlReady || current.refundCents != next.refundCents) {
            current = next;
            emit dataChanged(index(static_cast<int>(row), 3), index(static_cast<int>(row), 8));
        }
    }
    if (jobs.size() > oldSize) {
        beginInsertRows({}, static_cast<int>(oldSize), static_cast<int>(jobs.size()) - 1);
        for (qsizetype row = oldSize; row < jobs.size(); ++row) jobs_.append(jobs[row]);
        endInsertRows();
    }
}
StatisticsChart::StatisticsChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(100);
    setAccessibleName(QStringLiteral("任务状态统计图"));
}
void StatisticsChart::updateMetrics(const Statistics& metrics) { metrics_ = metrics; update(); }
void StatisticsChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const std::array<std::size_t, 5> values{metrics_.queued, metrics_.running + metrics_.cancelling,
        metrics_.completed, metrics_.failed, metrics_.cancelled};
    const QStringList labels{QStringLiteral("等待"), QStringLiteral("运行"), QStringLiteral("完成"), QStringLiteral("失败"), QStringLiteral("取消")};
    const std::array<QColor, 5> colors{QColor("#94B7AE"), QColor("#4548E8"), QColor("#4548E8"), QColor("#D89079"), QColor("#B0B8C8")};
    std::size_t total = 0;
    for (auto value : values) total += value;
    const QRectF bar(0, 6, width(), 12);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#E6EBF3")); p.drawRoundedRect(bar, 6, 6);
    QPainterPath clip;
    clip.addRoundedRect(bar, 6, 6);
    p.save(); p.setClipPath(clip);
    double x = 0;
    for (int i = 0; i < 5; ++i) {
        if (total == 0) break;
        const double w = static_cast<double>(values[i]) / static_cast<double>(total) * width();
        p.fillRect(QRectF(x, 6, w, 12), colors[i]); x += w;
    }
    p.restore();
    QFont textFont = font(); textFont.setPixelSize(11); p.setFont(textFont);
    const int column = width() / 3;
    for (int i = 0; i < 5; ++i) {
        const int left = (i % 3) * column;
        const int top = 35 + (i / 3) * 26;
        p.setPen(Qt::NoPen); p.setBrush(colors[i]); p.drawEllipse(QRectF(left, top + 6, 6, 6));
        p.setPen(QColor("#62687D"));
        p.drawText(QRect(left + 12, top, column - 12, 18), Qt::AlignVCenter,
                   labels[i] + "  " + QString::number(static_cast<qulonglong>(values[i])));
    }
}

PrintWindow::PrintWindow(WindowOptions options, QWidget* parent) : QMainWindow(parent) {
    const bool shopMode = options.shopMode;
    setWindowTitle(QStringLiteral("纸间 · 打印生产工作台"));
    setMinimumSize(1000, 680);
    const auto available = screen()->availableGeometry();
    resize(std::min(1420, available.width() - 40), std::min(900, available.height() - 60));
    ui::applyTheme(*this);
    auto* central = new QWidget(this); central->setObjectName("workspace");
    const auto shell = ui::studioShell(central, shopMode ? QStringLiteral("店主工作台") : QStringLiteral("队列调度"));
    auto* layout = shell.content;
    auto* header = new QHBoxLayout; header->setSpacing(10);
    auto* pageTitle = label(QStringLiteral("每一份，都有序抵达。"), "studioTitle"); header->addWidget(pageTitle);
    auto* navigation = new QButtonGroup(this); navigation->setExclusive(true);
    auto* stack = new QStackedWidget; stack->setObjectName("workspacePages");
    const QStringList names{QStringLiteral("工作台"), QStringLiteral("记录"), QStringLiteral("设备"), QStringLiteral("经营")};
    const QStringList navIds{"navWorkspace", "navHistory", "navDevices", "navShop"};
    for (int i = 0; i < 4; ++i) {
        auto* button = new QPushButton(names[i]); button->setObjectName(navIds[i]); button->setProperty("role", "nav");
        button->setCheckable(true); button->setChecked(i == 0); button->setToolTip(QStringLiteral("Ctrl+%1").arg(i + 1));
        navigation->addButton(button, i); shell.navigation->addWidget(button);
        connect(button, &QPushButton::clicked, this, [stack, i] { stack->setCurrentIndex(i); });
        connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this), &QShortcut::activated, this, [stack, i] { stack->setCurrentIndex(i); });
    }
    ui::finishRail(shell.navigation);
    connect(stack, &QStackedWidget::currentChanged, this, [navigation, pageTitle](int index) {
        navigation->button(index)->setChecked(true);
        pageTitle->setText(QStringList{QStringLiteral("每一份，都有序抵达。"), QStringLiteral("订单记录"), QStringLiteral("设备与执行"), QStringLiteral("店铺经营")}.value(index));
    });
    header->addStretch();
    auto* command = new QPushButton(QStringLiteral("命令  Ctrl+K")); command->setObjectName("commandPalette"); command->setProperty("role", "quiet");
    command->setToolTip(QStringLiteral("搜索并执行常用操作")); header->addWidget(command);
    QPushButton* student = nullptr;
    if (shopMode) {
        // The student client is a separate process talking to the same coordinator.
        student = new QPushButton(QStringLiteral("打开学生端")); student->setObjectName("openStudent"); student->setProperty("role", "quiet");
        student->setToolTip(QStringLiteral("启动学生自助下单界面，连接同一个后台。"));
        header->addWidget(student);
        connect(student, &QPushButton::clicked, this, [this, options] {
            QStringList arguments{"--role", "student", "--data-dir", options.stateDirectory, "--page-ms", QString::number(options.pageMs)};
            if (options.demoMode) arguments << "--demo-mode";
            if (QProcess::startDetached(cps::siblingExecutable("campus-print"), arguments)) ui::say(feedback_, QStringLiteral("已启动学生端，它与这里共用同一个后台。"));
            else ui::say(feedback_, QStringLiteral("无法启动学生端。"), "error");
        });
    }
    if (options.demoMode) {
        auto* demo = new QPushButton(QStringLiteral("演示任务")); demo->setObjectName("demoWorkload"); demo->setProperty("role", "quiet");
        demo->setToolTip(QStringLiteral("一次加入一批混合规格的演示订单。"));
        header->addWidget(demo); connect(demo, &QPushButton::clicked, this, &PrintWindow::loadDemo);
    }
    auto* newTask = new QPushButton(shopMode ? QStringLiteral("柜台下单") : QStringLiteral("新建任务")); newTask->setObjectName("newTask"); newTask->setProperty("role", "primary");
    newTask->setIcon(ui::icon(ui::Icon::Plus, Qt::white)); newTask->setToolTip(QStringLiteral("Ctrl+N")); header->addWidget(newTask); layout->addLayout(header);
    connect(new QShortcut(QKeySequence::New, this), &QShortcut::activated, newTask, &QPushButton::click);

    // Workbench: the illustration and queue share one clean surface; controls are native.
    auto* workArea = new QScrollArea; workArea->setObjectName("workbenchScroll");
    workArea->setWidgetResizable(true); workArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* work = new QWidget; work->setObjectName("workbench"); auto* body = new QVBoxLayout(work);
    body->setContentsMargins(0, 0, 0, 0); body->setSpacing(0); body->setSizeConstraint(QLayout::SetMinimumSize);
    scene_ = new PaperWorkspace; body->addWidget(scene_, 1);
    auto* controls = new QFrame; controls->setObjectName("timelineControls");
    controls->setStyleSheet("QFrame#timelineControls { background: white; border-top: 1px solid #E5EAF2; }");
    auto* controlRow = new QHBoxLayout(controls); controlRow->setContentsMargins(28, 17, 28, 10);
    controlRow->addWidget(label(QStringLiteral("设备与调度"), "section")); controlRow->addStretch();
    auto* policy = new SelectInput; policy->setObjectName("schedulingPolicy");
    policy->addItems({QStringLiteral("FCFS · 先来先服务"), QStringLiteral("SJF · 短作业优先"), QStringLiteral("优先级 + 等待老化"), QStringLiteral("交付期限优先"), QStringLiteral("营业收入导向")});
    policy->setMinimumWidth(190); policy->setToolTip(QStringLiteral("只影响等待中的订单，不会打断正在打印的订单。"));
    controlRow->addWidget(policy); controlRow->addStretch();
    auto* motion = new QPushButton(QStringLiteral("减少动效")); motion->setObjectName("reduceMotion"); motion->setCheckable(true); motion->setProperty("role", "quiet");
    controlRow->addWidget(motion);
    connect(motion, &QPushButton::toggled, this, [this](bool reduced) { scene_->setMotion(!reduced); });
    auto* pause = new QPushButton(QStringLiteral("暂停派发")); pause->setObjectName("pauseDispatch"); pause->setCheckable(true);
    pause->setIcon(ui::icon(ui::Icon::Pause)); pause->setToolTip(QStringLiteral("暂停领取新任务；已经运行的任务继续打印。"));
    controlRow->addWidget(pause); body->addWidget(controls);
    timeline_ = new PrinterTimeline; body->addWidget(timeline_);
    auto* workNote = label(QStringLiteral("文档图形为任务示意  ·  颜色区分内容层次，打印进度来自实际执行事件"), "muted");
    workNote->setContentsMargins(28, 9, 0, 9); body->addWidget(workNote);
    workArea->setWidget(work); stack->addWidget(workArea);

    auto* queueArea = new QScrollArea; queueArea->setObjectName("queueScrollArea");
    queueArea->setWidgetResizable(true); queueArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* queueContent = new QWidget; queueContent->setObjectName("queueContent");
    auto* working = new QVBoxLayout(queueContent); working->setContentsMargins(0, 0, 0, 0); working->setSpacing(14);
    working->setSizeConstraint(QLayout::SetMinimumSize);
    auto* metrics = new QHBoxLayout;
    const QStringList metricTitles{QStringLiteral("已完成任务"), QStringLiteral("等待队列"), QStringLiteral("平均首次等待"), QStringLiteral("设备利用率")};
    for (int i = 0; i < 4; ++i) {
        auto* box = card("metric"); auto* m = new QVBoxLayout(box); m->setContentsMargins(18, 12, 18, 12);
        m->addWidget(label(metricTitles[i], "metricLabel")); metricValues_[i] = label("0", "metricValue"); m->addWidget(metricValues_[i]);
        metricHints_[i] = label(QStringLiteral("本次会话"), "metricHint"); m->addWidget(metricHints_[i]); metrics->addWidget(box);
    }
    working->addLayout(metrics);
    auto* queue = card(); auto* queueLayout = new QVBoxLayout(queue); queueLayout->setContentsMargins(18, 16, 18, 14); queueLayout->setSpacing(10);
    auto* actions = new QHBoxLayout; actions->setSpacing(8); actions->addWidget(label(QStringLiteral("任务记录"), "section"));
    queueSummary_ = label(QStringLiteral("0 个任务"), "muted"); actions->addWidget(queueSummary_); actions->addStretch();
    auto* search = new QLineEdit; search->setObjectName("queueSearch"); search->setClearButtonEnabled(true); search->setFixedWidth(220);
    search->setPlaceholderText(shopMode ? QStringLiteral("搜索文档、取件码或学号") : QStringLiteral("搜索文档或任务 ID"));
    search->addAction(ui::icon(ui::Icon::Search), QLineEdit::LeadingPosition); search->setToolTip(QStringLiteral("Ctrl+F"));
    actions->addWidget(search);
    auto* filter = new SelectInput; filter->setObjectName("queueFilter");
    filter->addItems({QStringLiteral("全部任务"), QStringLiteral("等待中"), QStringLiteral("打印中"), QStringLiteral("已完成"), QStringLiteral("失败"), QStringLiteral("已取消")});
    actions->addWidget(filter);
    auto* report = new QPushButton(QStringLiteral("导出报表")); report->setIcon(ui::icon(ui::Icon::Export)); actions->addWidget(report);
    refund_ = new QPushButton(QStringLiteral("退款")); refund_->setObjectName("refundOrder"); refund_->setProperty("role", "danger");
    refund_->setEnabled(false); refund_->setVisible(shopMode); actions->addWidget(refund_);
    cancel_ = new QPushButton(QStringLiteral("取消任务")); cancel_->setObjectName("cancelJob"); cancel_->setProperty("role", "danger"); cancel_->setEnabled(false); actions->addWidget(cancel_);
    queueLayout->addLayout(actions);
    // Two stock proxies in a row: the state filter, then the search box over the hidden text column.
    model_ = new JobTableModel(this);
    auto* states = new QSortFilterProxyModel(this); states->setSourceModel(model_); states->setFilterKeyColumn(3);
    filter_ = new QSortFilterProxyModel(this); filter_->setSourceModel(states); filter_->setFilterKeyColumn(10);
    filter_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    table_ = new QTableView; table_->setObjectName("jobsTable"); table_->setModel(filter_); table_->setItemDelegate(new QueueDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setShowGrid(false); table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight); table_->setMinimumHeight(220); table_->setMouseTracking(true);
    table_->verticalHeader()->setVisible(false); table_->verticalHeader()->setDefaultSectionSize(58);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter); table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->setColumnWidth(1, 65); table_->setColumnWidth(2, 65); table_->setColumnWidth(3, 90);
    table_->setColumnWidth(4, 130); table_->setColumnWidth(5, 190); table_->setColumnWidth(7, 125); table_->setColumnWidth(8, 150); table_->setColumnWidth(9, 120);
    table_->hideColumn(6); table_->hideColumn(10);
    // The legacy queue has no pricing; the shop has no manual priority column.
    if (shopMode) table_->hideColumn(2);
    else for (int column = 7; column <= 9; ++column) table_->hideColumn(column);
    // The document column takes the remaining width but never collapses; a narrow window scrolls sideways instead.
    table_->viewport()->installEventFilter(this);
    queueLayout->addWidget(table_, 1);
    emptyState_ = ui::emptyState(table_, QStringLiteral("这里暂时没有任务"), QStringLiteral("新建任务，或调整搜索与状态筛选"));
    if (options.demoMode) {
        auto* demo = new QHBoxLayout; demo->addWidget(label(QStringLiteral("故障恢复演示"), "muted")); demo->addStretch();
        fault_ = new QPushButton(QStringLiteral("模拟进程崩溃")); fault_->setObjectName("injectFault"); fault_->setEnabled(false); demo->addWidget(fault_);
        disconnect_ = new QPushButton(QStringLiteral("断开控制连接")); disconnect_->setObjectName("disconnectControl"); disconnect_->setEnabled(false); demo->addWidget(disconnect_);
        queueLayout->addLayout(demo);
    }
    working->addWidget(queue, 1); queueArea->setWidget(queueContent); stack->addWidget(queueArea);
    auto focusSearch = [stack, search] { stack->setCurrentIndex(1); search->setFocus(); search->selectAll(); };
    connect(new QShortcut(QKeySequence::Find, this), &QShortcut::activated, this, focusSearch);

    auto* deviceArea = new QScrollArea; deviceArea->setObjectName("deviceScroll");
    deviceArea->setWidgetResizable(true); deviceArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* devices = new QWidget; devices->setProperty("role", "page");
    auto* deviceLayout = new QVBoxLayout(devices); deviceLayout->setContentsMargins(0, 0, 0, 0); deviceLayout->setSpacing(14);
    auto* fleetCard = card(); auto* fleetLayout = new QVBoxLayout(fleetCard); fleetLayout->setContentsMargins(18, 16, 18, 14); fleetLayout->setSpacing(10);
    auto* deviceHead = new QHBoxLayout; deviceHead->setSpacing(8); deviceHead->addWidget(label(QStringLiteral("设备与执行状态"), "section"));
    fleetSummary_ = label({}, "muted"); fleetSummary_->setObjectName("fleetSummary"); deviceHead->addWidget(fleetSummary_); deviceHead->addStretch();
    auto* check = new QPushButton(QStringLiteral("检查执行器")); check->setIcon(ui::icon(ui::Icon::Refresh)); deviceHead->addWidget(check);
    auto* folder = new QPushButton(QStringLiteral("打开输出目录")); folder->setIcon(ui::icon(ui::Icon::Folder)); deviceHead->addWidget(folder);
    fleetLayout->addLayout(deviceHead);
    connect(folder, &QPushButton::clicked, this, [this, options] {
        const auto output = QDir(options.stateDirectory).filePath("output");
        if (!QDir().mkpath(output) || !QDesktopServices::openUrl(QUrl::fromLocalFile(output))) ui::say(feedback_, QStringLiteral("无法打开输出目录。"), "error");
    });
    fleetTable_ = ui::listTable("fleetTable", {QStringLiteral("设备"), QStringLiteral("能力"), QStringLiteral("模拟秒/面 · 黑白 / 彩色"), QStringLiteral("接单状态"), QStringLiteral("当前订单"), QStringLiteral("进度")});
    fleetLayout->addWidget(fleetTable_);
    auto* deviceActions = new QHBoxLayout; deviceActions->setSpacing(8);
    deviceActions->addWidget(label(shopMode ? QStringLiteral("选中一台设备后可暂停或恢复接单；双击空闲设备可编辑。") : QStringLiteral("经营模式下可逐台暂停接单和编辑设备。"), "muted"));
    deviceActions->addStretch();
    toggleDevice_ = new QPushButton(QStringLiteral("暂停接单")); toggleDevice_->setObjectName("togglePrinter"); toggleDevice_->setEnabled(false);
    editDevice_ = new QPushButton(QStringLiteral("编辑设备")); editDevice_->setObjectName("editPrinter"); editDevice_->setEnabled(false);
    deviceActions->addWidget(toggleDevice_); deviceActions->addWidget(editDevice_); fleetLayout->addLayout(deviceActions);
    deviceLayout->addWidget(fleetCard);
    connect(fleetTable_, &QTableWidget::itemSelectionChanged, this, &PrintWindow::updateDeviceActions);
    connect(toggleDevice_, &QPushButton::clicked, this, [this] {
        const int i = fleetTable_->currentRow(); const auto fleet = shopSnapshot_.value("fleet").toArray();
        if (i < 0 || i >= fleet.size()) return;
        emit shopRequested("set_printer", {{"index", i}, {"online", !fleet[i].toObject().value("online").toBool()}});
    });
    auto editDevice = [this] {
        const int i = fleetTable_->currentRow(); const auto fleet = shopSnapshot_.value("fleet").toArray();
        if (!editDevice_->isEnabled() || i < 0 || i >= fleet.size()) return;
        auto object = fleet[i].toObject(); QDialog dialog(this); dialog.setObjectName("deviceDialog");
        dialog.setWindowTitle(QStringLiteral("编辑设备")); dialog.setFixedWidth(420);
        auto* fields = new QVBoxLayout(&dialog); fields->setContentsMargins(28, 24, 28, 22); fields->setSpacing(10);
        fields->addWidget(label(QStringLiteral("编辑设备"), "section"));
        fields->addWidget(label(QStringLiteral("改动只在设备空闲时生效，不影响已经完成的订单。"), "muted")); fields->addSpacing(2);
        fields->addWidget(label(QStringLiteral("设备名称"), "muted"));
        auto* name = new QLineEdit(object.value("name").toString()); fields->addWidget(name);
        fields->addWidget(label(QStringLiteral("能力"), "muted"));
        auto* abilities = new QHBoxLayout; abilities->setSpacing(18);
        auto* wide = new ui::CheckBox(QStringLiteral("A3 纸张")); wide->setChecked(object.value("a3").toBool()); abilities->addWidget(wide);
        auto* colorful = new ui::CheckBox(QStringLiteral("彩色")); colorful->setChecked(object.value("color").toBool()); abilities->addWidget(colorful);
        auto* twoSided = new ui::CheckBox(QStringLiteral("双面")); twoSided->setChecked(object.value("duplex").toBool()); abilities->addWidget(twoSided);
        abilities->addStretch(); fields->addLayout(abilities);
        fields->addWidget(label(QStringLiteral("速度 · 毫秒每面（A3 加倍）"), "muted"));
        auto* speeds = new QHBoxLayout; speeds->setSpacing(10);
        auto* monoMs = new NumberInput; monoMs->setRange(10, 5000); monoMs->setValue(object.value("mono_page_ms").toInt()); monoMs->setPrefix(QStringLiteral("黑白  "));
        auto* colorMs = new NumberInput; colorMs->setRange(10, 5000); colorMs->setValue(object.value("color_page_ms").toInt()); colorMs->setPrefix(QStringLiteral("彩色  "));
        speeds->addWidget(monoMs); speeds->addWidget(colorMs); fields->addLayout(speeds); fields->addSpacing(6);
        auto* choices = new QHBoxLayout; choices->setSpacing(10);
        auto* dismiss = new QPushButton(QStringLiteral("取消")); choices->addWidget(dismiss, 1);
        auto* save = new QPushButton(QStringLiteral("保存设备")); save->setProperty("role", "primary"); save->setDefault(true); choices->addWidget(save, 1);
        fields->addLayout(choices);
        connect(dismiss, &QPushButton::clicked, &dialog, &QDialog::reject); connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted) return;
        object.insert("name", name->text()); object.insert("a3", wide->isChecked()); object.insert("color", colorful->isChecked()); object.insert("duplex", twoSided->isChecked());
        object.insert("mono_page_ms", monoMs->value()); object.insert("color_page_ms", colorMs->value());
        emit shopRequested("set_printer", {{"index", i}, {"printer", object}});
    };
    connect(editDevice_, &QPushButton::clicked, this, editDevice);
    connect(fleetTable_, &QTableWidget::cellDoubleClicked, this, editDevice);
    auto* distribution = card(); auto* dist = new QVBoxLayout(distribution); dist->setContentsMargins(20, 16, 20, 12); dist->setSpacing(10);
    dist->addWidget(label(QStringLiteral("本次任务分布"), "cardTitle")); chart_ = new StatisticsChart; dist->addWidget(chart_); deviceLayout->addWidget(distribution);
    auto* note = label(QStringLiteral("本地模拟打印。暂停设备接单后，当前订单会继续完成。\n队列由独立后台保存；关闭窗口后任务继续执行，重开可查看最新结果。"), "muted");
    note->setWordWrap(true); deviceLayout->addWidget(note); deviceLayout->addStretch();
    deviceArea->setWidget(devices); stack->addWidget(deviceArea);

    auto* shopPage = new QWidget; shopPage->setObjectName("shopContent");
    auto* shopLayout = new QVBoxLayout(shopPage); shopLayout->setContentsMargins(0, 0, 14, 0); shopLayout->setSpacing(14);
    shopLayout->setSizeConstraint(QLayout::SetMinimumSize);
    auto* shopHead = new QHBoxLayout; shopHead->setSpacing(10); shopHead->addWidget(label(QStringLiteral("经营概览"), "section"));
    shopHead->addWidget(label(QStringLiteral("模拟经营 · 完成后结算 · 报价与期限在下单时锁定"), "muted")); shopHead->addStretch();
    shopLayout->addLayout(shopHead);
    auto* shopMetrics = new QHBoxLayout;
    const QStringList shopTitles{QStringLiteral("实收营业额"), QStringLiteral("在线预收"), QStringLiteral("等待订单"), QStringLiteral("设备与库存")};
    for (int i = 0; i < 4; ++i) {
        auto* box = card("metric"); auto* m = new QVBoxLayout(box); m->setContentsMargins(18, 12, 18, 12);
        m->addWidget(label(shopTitles[i], "metricLabel"));
        shopValues_[i] = label("—", "metricValue"); shopValues_[i]->setObjectName(QStringLiteral("shopMetric%1").arg(i)); m->addWidget(shopValues_[i]);
        shopHints_[i] = label({}, "metricHint"); m->addWidget(shopHints_[i]); shopMetrics->addWidget(box);
    }
    shopLayout->addLayout(shopMetrics);
    auto* books = new QHBoxLayout; books->setSpacing(14);
    auto* ledger = card(); auto* ledgerLayout = new QVBoxLayout(ledger); ledgerLayout->setContentsMargins(20, 16, 20, 16); ledgerLayout->setSpacing(12);
    ledgerLayout->addWidget(label(QStringLiteral("收支与履约"), "cardTitle"));
    auto* figures = new QGridLayout; figures->setHorizontalSpacing(16); figures->setVerticalSpacing(14);
    const QStringList ledgerTitles{QStringLiteral("直接成本"), QStringLiteral("贡献利润"), QStringLiteral("累计退款"), QStringLiteral("待完成订单金额"),
        QStringLiteral("微信实收"), QStringLiteral("支付宝实收"), QStringLiteral("校园卡实收"), QStringLiteral("柜台实收"),
        QStringLiteral("已完成订单"), QStringLiteral("按时完成"), QStringLiteral("超时完成"), QStringLiteral("当前逾期")};
    const QStringList ledgerIds{"ledgerCost", "ledgerContribution", "ledgerRefund", "ledgerPending", "paid_wechat", "paid_alipay", "paid_campus_card", "paid_counter",
        "ordersCompleted", "ordersOnTime", "ordersLate", "ordersOverdue"};
    for (int i = 0; i < 12; ++i) {
        auto* cell = new QVBoxLayout; cell->setSpacing(2); cell->addWidget(label(ledgerTitles[i], "statLabel"));
        ledger_[i] = label(QStringLiteral("—"), "statValue"); ledger_[i]->setObjectName(ledgerIds[i]); cell->addWidget(ledger_[i]);
        figures->addLayout(cell, i / 4, i % 4);
    }
    ledgerLayout->addLayout(figures);
    // Only shown when there are no figures to show: before the first snapshot, or for a legacy queue.
    shopSummary_ = label(QStringLiteral("等待后台经营数据"), "body"); shopSummary_->setObjectName("shopSummary"); shopSummary_->setWordWrap(true);
    ledgerLayout->addWidget(shopSummary_); ledgerLayout->addStretch(); books->addWidget(ledger, 3);
    auto* supplies = card(); auto* suppliesLayout = new QVBoxLayout(supplies); suppliesLayout->setContentsMargins(20, 16, 20, 16); suppliesLayout->setSpacing(12);
    suppliesLayout->addWidget(label(QStringLiteral("库存与队列"), "cardTitle"));
    const QStringList papers{"a4", "a3"};
    for (int i = 0; i < 2; ++i) {
        const auto size = papers[i];
        auto* row = new QHBoxLayout; auto* cell = new QVBoxLayout; cell->setSpacing(2);
        cell->addWidget(label(QStringLiteral("%1 可用库存").arg(size.toUpper()), "statLabel"));
        stock_[i] = label(QStringLiteral("—"), "statValue"); stock_[i]->setObjectName("stock_" + size); cell->addWidget(stock_[i]);
        row->addLayout(cell); row->addStretch();
        auto* restock = new QPushButton(QStringLiteral("补充 %1 纸张").arg(size.toUpper())); restock->setObjectName("restock_" + size); restock->setEnabled(false);
        row->addWidget(restock); suppliesLayout->addLayout(row);
        connect(restock, &QPushButton::clicked, this, [this, size] {
            bool ok = false; const int n = QInputDialog::getInt(this, QStringLiteral("补充库存"), QStringLiteral("增加 %1 纸张张数").arg(size.toUpper()), 500, 1, 1000000, 1, &ok);
            if (ok) emit shopRequested("restock", {{"paper", size}, {"quantity", n}});
        });
    }
    shopForecast_ = label({}, "body"); shopForecast_->setObjectName("shopForecast"); shopForecast_->setWordWrap(true); suppliesLayout->addWidget(shopForecast_);
    auto* stockNote = label(QStringLiteral("运行与重试中的订单已预留纸张。直接成本为成功订单的纸张与墨粉估算，未计租金、人工或故障废页。"), "muted");
    stockNote->setWordWrap(true); suppliesLayout->addWidget(stockNote); suppliesLayout->addStretch(); books->addWidget(supplies, 2);
    shopLayout->addLayout(books);
    auto* lab = card(); auto* labLayout = new QVBoxLayout(lab); labLayout->setContentsMargins(20, 16, 20, 14); labLayout->setSpacing(10);
    auto* labHead = new QHBoxLayout; labHead->setSpacing(8); labHead->addWidget(label(QStringLiteral("策略对比实验"), "cardTitle")); labHead->addStretch();
    labHead->addWidget(label(QStringLiteral("模拟客户数"), "muted"));
    auto* experimentCount = new NumberInput; experimentCount->setRange(1, 2000); experimentCount->setValue(600); experimentCount->setObjectName("experimentCustomers");
    experimentCount->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); experimentCount->setFixedWidth(96); labHead->addWidget(experimentCount);
    auto* runExperiment = new QPushButton(QStringLiteral("运行策略对比")); runExperiment->setObjectName("runShopExperiments"); labHead->addWidget(runExperiment);
    labLayout->addLayout(labHead);
    auto* experimentTable = ui::listTable("experimentTable", {QStringLiteral("实验场景"), QStringLiteral("策略"), QStringLiteral("优惠"), QStringLiteral("接单/流失"), QStringLiteral("营业额"), QStringLiteral("贡献利润"), QStringLiteral("完成/超时"), QStringLiteral("期末未完成")});
    experimentTable->setSelectionMode(QAbstractItemView::NoSelection); experimentTable->setMinimumHeight(240); labLayout->addWidget(experimentTable, 1);
    auto* experimentEmpty = ui::emptyState(experimentTable, QStringLiteral("还没有实验结果"), QStringLiteral("运行后在这里比较 21 组策略与优惠方案，完整数据另存为 JSON"));
    auto* experimentNote = label(QStringLiteral("固定随机种子与 8 小时营业窗口。预算敏感客户占比 0%、30%、60%；完整报表含假设、客户输入与派发记录。"), "muted");
    experimentNote->setWordWrap(true); labLayout->addWidget(experimentNote); shopLayout->addWidget(lab, 1);
    auto* pricing = card(); auto* pricingLayout = new QVBoxLayout(pricing); pricingLayout->setContentsMargins(20, 16, 20, 16); pricingLayout->setSpacing(10);
    pricingLayout->addWidget(label(QStringLiteral("计价与口径"), "cardTitle"));
    auto* tiers = new QHBoxLayout; tiers->setSpacing(16);
    const QStringList tierNames{QStringLiteral("快速"), QStringLiteral("普通"), QStringLiteral("灵活")};
    const QStringList tierTerms{QStringLiteral("30 分钟内 · 原价"), QStringLiteral("2 小时内 · 95 折"), QStringLiteral("8 小时内 · 85 折")};
    for (int i = 0; i < 3; ++i) {
        auto* cell = new QVBoxLayout; cell->setSpacing(2);
        cell->addWidget(label(tierNames[i], "statLabel")); cell->addWidget(label(tierTerms[i], "cardTitle")); tiers->addLayout(cell, 1);
    }
    pricingLayout->addLayout(tiers);
    auto* tierNote = label(QStringLiteral("A4 黑白 0.20 元/面，彩色 1.50 元/面；A3 加倍。所有价格和成本均为演示参数。\n交付时间为承诺期限，超时会单独统计；设备暂停或缺纸可能影响履约。\n预计时间按当前队列、策略与设备推算，不含之后到达的订单，不是承诺。学生端在线支付为模拟，取消或失败的订单不计收入。"), "muted");
    tierNote->setWordWrap(true); pricingLayout->addWidget(tierNote); shopLayout->addWidget(pricing);
    auto* shopScroll = new QScrollArea; shopScroll->setObjectName("shopScroll"); shopScroll->setWidgetResizable(true);
    shopScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shopScroll->setWidget(shopPage); stack->addWidget(shopScroll);
    connect(runExperiment, &QPushButton::clicked, this, [this, options, experimentCount] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存策略实验"), options.stateDirectory + "/shop-experiments.json", "JSON (*.json)");
        if (!path.isEmpty()) emit experimentRequested(path, experimentCount->value());
    });
    connect(refund_, &QPushButton::clicked, this, [this] {
        const auto index = table_->currentIndex();
        if (!index.isValid() || index.data(Qt::UserRole + 1).toInt() != int(JobState::Completed)) return;
        const auto id = index.data(Qt::UserRole).toString(); const double cents = index.data(Qt::UserRole + 5).toDouble();
        ui::confirm(this, "refundDialog", QStringLiteral("为这笔订单全额退款？"),
            QStringLiteral("%1\n退回 %2（模拟）。退款会从实收营业额中扣除，确认后无法撤销。")
                .arg(index.sibling(index.row(), 0).data().toString(), text::money(Money(cents))),
            QStringLiteral("确认退款"), [this, id, cents] { emit shopRequested("refund", {{"job_id", id}, {"cumulative_cents", cents}}); });
    });
    if (shopMode) stack->setCurrentIndex(3);
    layout->addWidget(stack, 1);
    auto* footer = new QHBoxLayout; footer->setSpacing(12);
    connection_ = label(QStringLiteral("检查执行器…"), "badge"); footer->addWidget(connection_);
    feedback_ = label(QStringLiteral("新建一份任务，或加载演示任务体验经营调度。"), "muted");
    feedback_->setObjectName("feedback"); feedback_->setWordWrap(true); footer->addWidget(feedback_, 1);
    logToggle_ = new QToolButton; logToggle_->setObjectName("logToggle"); logToggle_->setCheckable(true); logToggle_->setProperty("role", "quiet");
    logToggle_->setText(QStringLiteral("运行日志")); footer->addWidget(logToggle_); layout->addLayout(footer);
    log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->setMaximumBlockCount(500); log_->setFixedHeight(120); log_->hide();
    layout->addWidget(log_); connect(logToggle_, &QToolButton::toggled, log_, &QWidget::setVisible); setCentralWidget(central);

    auto* submission = new QDialog(this); submission->setObjectName("submissionDialog");
    submission->setWindowTitle(shopMode ? QStringLiteral("柜台下单") : QStringLiteral("新建打印任务"));
    submission->setWindowModality(Qt::WindowModal); submission->setFixedWidth(470);
    auto* form = new QVBoxLayout(submission); form->setContentsMargins(28, 24, 28, 22); form->setSpacing(10);
    form->addWidget(label(shopMode ? QStringLiteral("为到店顾客下单") : QStringLiteral("准备打印什么？"), "section"));
    form->addWidget(label(shopMode ? QStringLiteral("报价与取件期限在下单时锁定，记为柜台收款。") : QStringLiteral("输入文档信息，交给调度器安排。"), "muted"));
    form->addSpacing(2); form->addWidget(label(QStringLiteral("文档名称"), "muted"));
    auto* title = new QLineEdit(QStringLiteral("操作系统实验报告")); title->setObjectName("jobTitle"); title->setMaxLength(200); form->addWidget(title);
    auto* pages = new NumberInput; pages->setObjectName("jobPages"); pages->setRange(1, 1000); pages->setValue(8);
    auto* copies = new NumberInput; copies->setObjectName("jobCopies"); copies->setRange(1, 100); copies->setValue(1);
    auto* priority = new NumberInput; priority->setObjectName("jobPriority"); priority->setRange(0, 100);
    const std::array<QSpinBox*, 3> inputs{pages, copies, priority};
    auto* numbers = new QHBoxLayout;
    for (int i = 0; i < 3; ++i) {
        auto* field = new QVBoxLayout; field->addWidget(label(QStringList{QStringLiteral("页数"),QStringLiteral("份数"),QStringLiteral("优先级")}[i], "muted"));
        field->addWidget(inputs[i]); numbers->addLayout(field);
    }
    form->addLayout(numbers);
    auto* paper = new SelectInput; paper->setObjectName("orderPaper"); paper->addItems({"A4", "A3"});
    auto* color = new ui::CheckBox(QStringLiteral("彩色打印")); color->setObjectName("orderColor");
    auto* duplex = new ui::CheckBox(QStringLiteral("双面打印")); duplex->setObjectName("orderDuplex");
    auto* service = new SelectInput; service->setObjectName("orderService");
    service->addItems({QStringLiteral("快速 · 30 分钟 · 原价"), QStringLiteral("普通 · 2 小时 · 95 折"), QStringLiteral("灵活 · 8 小时 · 85 折")}); service->setCurrentIndex(1);
    // Pricing options only exist in the shop mode; the legacy queue keeps the short form.
    auto* shopFields = new QWidget; auto* shopForm = new QVBoxLayout(shopFields); shopForm->setContentsMargins(0, 0, 0, 0); shopForm->setSpacing(10);
    shopForm->addWidget(label(QStringLiteral("纸张与打印规格"), "muted"));
    auto* specification = new QHBoxLayout; specification->setSpacing(16);
    specification->addWidget(paper, 1); specification->addWidget(color); specification->addWidget(duplex); shopForm->addLayout(specification);
    shopForm->addWidget(label(QStringLiteral("取件期限与优惠"), "muted")); shopForm->addWidget(service);
    form->addWidget(shopFields); shopFields->setVisible(shopMode);
    paper->setEnabled(shopMode); color->setEnabled(shopMode); duplex->setEnabled(shopMode); service->setEnabled(shopMode);
    auto* summary = card("summary"); auto* summaryLayout = new QVBoxLayout(summary); summaryLayout->setContentsMargins(14, 10, 14, 10); summaryLayout->setSpacing(3);
    auto* price = label({}, "statValue"); price->setObjectName("orderPrice"); summaryLayout->addWidget(price);
    // Minimum widths make the wrapped lines report the height they need inside the fixed-width dialog.
    auto* sheets = label(QStringLiteral("本次打印共 8 页"), "muted"); sheets->setObjectName("orderSheets"); sheets->setWordWrap(true); sheets->setMinimumWidth(380); summaryLayout->addWidget(sheets);
    auto* eta = label({}, "body"); eta->setObjectName("orderEta"); eta->setWordWrap(true); eta->setMinimumWidth(380); eta->setVisible(shopMode); summaryLayout->addWidget(eta);
    form->addSpacing(2); form->addWidget(summary);
    auto orderJson = [=] {
        return QJsonObject{{"title", title->text()}, {"pages", pages->value()}, {"copies", copies->value()}, {"priority", priority->value()},
            {"paper", paper->currentIndex() == 1 ? "a3" : "a4"}, {"color", color->isChecked()}, {"duplex", duplex->isChecked()},
            {"service", QStringList{"express", "standard", "flexible"}[service->currentIndex()]}};
    };
    auto sheetSummary = [=] {
        if (pages->value() * copies->value() > 10000) {
            price->setText(QStringLiteral("—")); ui::say(sheets, QStringLiteral("总页数不得超过 10000"), "error"); return;
        }
        if (options.shopMode) {
            auto probe = orderJson(); probe.insert("title", QStringLiteral("quote"));
            emit quoteRequested(probe, ++quoteToken_);
        }
        Job job; job.pages = pages->value(); job.copies = copies->value(); job.paper = paper->currentIndex() == 1 ? PaperSize::A3 : PaperSize::A4;
        job.color = color->isChecked(); job.duplex = duplex->isChecked(); job.service = static_cast<ServiceTier>(service->currentIndex()); cps::quote(job);
        const bool priced = paper->isEnabled();
        price->setText(priced ? text::money(job.quotedCents) : QStringLiteral("%1 页").arg(job.sheets()));
        ui::say(sheets, priced ? QStringLiteral("%1 面 · %2 张纸 · 锁定报价（模拟）").arg(job.sheets()).arg(job.paperSheets())
                               : QStringLiteral("本次打印的总页数"));
    };
    connect(pages, &QSpinBox::valueChanged, this, sheetSummary); connect(copies, &QSpinBox::valueChanged, this, sheetSummary);
    connect(paper, &QComboBox::currentIndexChanged, this, sheetSummary); connect(service, &QComboBox::currentIndexChanged, this, sheetSummary);
    connect(color, &QCheckBox::toggled, this, sheetSummary); connect(duplex, &QCheckBox::toggled, this, sheetSummary); sheetSummary();
    auto* submit = new QPushButton(QStringLiteral("加入打印队列")); submit->setObjectName("submitJob"); submit->setProperty("role", "primary"); submit->setDefault(true); form->addWidget(submit);
    auto* secondary = new QHBoxLayout; secondary->setSpacing(10);
    auto* import = new QPushButton(QStringLiteral("从 JSON 文件批量导入")); import->setObjectName("importJobs"); import->setAutoDefault(false); secondary->addWidget(import, 1);
    auto* dismiss = new QPushButton(QStringLiteral("取消")); dismiss->setObjectName("dismissJob"); dismiss->setAutoDefault(false); secondary->addWidget(dismiss);
    form->addLayout(secondary); connect(dismiss, &QPushButton::clicked, submission, &QDialog::reject);
    connect(newTask, &QPushButton::clicked, this, [=] { sheetSummary(); submission->show(); submission->raise(); title->setFocus(); title->selectAll(); });
    qRegisterMetaType<ViewState>();
    controller_ = new PrintController(options); controller_->moveToThread(&backendThread_);
    connect(&backendThread_, &QThread::started, controller_, &PrintController::initialize);
    connect(&backendThread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(this, &PrintWindow::experimentRequested, controller_, &PrintController::runExperiments, Qt::QueuedConnection);
    connect(controller_, &PrintController::experimentReady, this, [experimentTable, experimentEmpty](const QJsonObject& report) {
        const auto runs = report.value("runs").toArray(); experimentTable->setRowCount(static_cast<int>(runs.size()));
        experimentEmpty->setVisible(runs.isEmpty());
        for (int i = 0; i < runs.size(); ++i) {
            const auto r = runs[i].toObject();
            const auto policyName = r.value("policy").toString();
            const QList<ui::Cell> cells{r.value("mode") == "fixed_orders" ? QStringLiteral("固定订单") : QStringLiteral("预算敏感 %1%").arg(r.value("budget_sensitive_percent").toInt()),
                {policyName == "revenue" ? QStringLiteral("收入导向") : policyName == "deadline" ? QStringLiteral("期限优先") : QStringLiteral("先来先服务"), "", -1, "strong"},
                {r.value("discounts").toBool() ? QStringLiteral("开启") : QStringLiteral("关闭"), r.value("discounts").toBool() ? "info" : "muted"},
                QStringLiteral("%1 / %2").arg(r.value("accepted").toInt()).arg(r.value("lost_before_order").toInt()),
                QString::number(r.value("revenue_cents").toDouble()/100, 'f', 2), QString::number(r.value("contribution_cents").toDouble()/100, 'f', 2),
                QStringLiteral("%1 / %2").arg(r.value("completed").toInt()).arg(r.value("late").toInt()), QString::number(r.value("unfinished_at_close").toInt())};
            for (int c = 0; c < cells.size(); ++c) ui::setCell(experimentTable, i, c, cells[c]);
        }
    }, Qt::QueuedConnection);
    connect(this, &PrintWindow::orderRequested, controller_, &PrintController::submitOrder, Qt::QueuedConnection);
    connect(this, &PrintWindow::quoteRequested, controller_, &PrintController::requestQuote, Qt::QueuedConnection);
    connect(controller_, &PrintController::quoteReady, this, [this, eta](int token, const QJsonObject& quote) {
        if (token != quoteToken_) return;
        eta->setText(QStringLiteral("现在下单：") + text::quoteForecast(quote));
        tone(eta, quote.value("type").toString() != QLatin1String("quote") ? "error" : quote.value("estimated_finish_ms").isDouble() ? "" : "waiting");
    }, Qt::QueuedConnection);
    connect(this, &PrintWindow::shopRequested, controller_, &PrintController::shopCommand, Qt::QueuedConnection);
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
    connect(controller_, &PrintController::feedback, this, [this](const QString& message, bool problem) {
        ui::say(feedback_, message, problem ? "error" : "");
    }, Qt::QueuedConnection);
    connect(controller_, &PrintController::logsReady, this, [this](const QStringList& lines) {
        log_->appendPlainText(lines.join('\n'));
    }, Qt::QueuedConnection);
    connect(submit, &QPushButton::clicked, this, [=] {
        if (title->text().trimmed().isEmpty() || pages->value() * copies->value() > 10000) {
            const auto error = QStringLiteral("请输入有效名称，总页数不得超过 10000。");
            ui::say(sheets, error, "error"); ui::say(feedback_, error, "error"); return;
        }
        if (options.shopMode) emit orderRequested(orderJson());
        else emit submitRequested(title->text(), pages->value(), copies->value(), priority->value());
        submission->accept();
    });
    connect(import, &QPushButton::clicked, this, [this, options] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入模拟任务数组"), options.stateDirectory, "JSON (*.json)");
        if (!path.isEmpty()) { importFile(path); }
    });
    connect(policy, &QComboBox::currentIndexChanged, this, &PrintWindow::policyRequested);
    connect(pause, &QPushButton::toggled, this, [this, pause](bool paused) {
        pause->setText(paused ? QStringLiteral("继续派发") : QStringLiteral("暂停派发"));
        pause->setIcon(ui::icon(paused ? ui::Icon::Play : ui::Icon::Pause));
        emit pauseRequested(paused);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { emit cancelRequested(table_->currentIndex().data(Qt::UserRole).toString()); });
    connect(check, &QPushButton::clicked, this, &PrintWindow::checkRequested);
    connect(report, &QPushButton::clicked, this, [this, options] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出持久队列统计"), options.stateDirectory + "/report.json", "JSON (*.json)");
        if (!path.isEmpty()) emit exportRequested(path);
    });
    connect(filter, &QComboBox::currentIndexChanged, this, [this, states, filter](int index) {
        states->setFilterFixedString(index == 0 ? QString() : filter->currentText());
        updateSelection();
    });
    connect(search, &QLineEdit::textChanged, this, [this](const QString& query) {
        filter_->setFilterFixedString(query.trimmed());
        updateSelection();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PrintWindow::updateSelection);
    scene_->selectJob = [this](const QString& id) {
        for (int row=0; row<filter_->rowCount(); ++row) {
            if (filter_->index(row,0).data(Qt::UserRole).toString() != id) continue;
            table_->selectRow(row); table_->scrollTo(filter_->index(row,0)); updateSelection(); break;
        }
    };
    auto toggleFullscreen = [this] { isFullScreen() ? showNormal() : showFullScreen(); };

    auto* fullKey = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullKey, &QShortcut::activated, this, toggleFullscreen);
    auto showCommands = [=] {
        QDialog palette(this); palette.setObjectName("commandDialog"); palette.setWindowTitle(QStringLiteral("命令中心"));
        palette.setMinimumWidth(480); palette.resize(520, 460);
        auto* content = new QVBoxLayout(&palette); content->setContentsMargins(20,20,20,20); content->setSpacing(12);
        content->addWidget(label(QStringLiteral("你想做什么？"), "section"));
        auto* query = new QLineEdit; query->setObjectName("commandQuery"); query->setPlaceholderText(QStringLiteral("输入命令关键词，Enter 执行")); content->addWidget(query);
        query->addAction(ui::icon(ui::Icon::Search), QLineEdit::LeadingPosition);
        auto* choices = new QListWidget; choices->setObjectName("commandChoices"); content->addWidget(choices);
        content->addWidget(label(QStringLiteral("↑ ↓ 选择 · Enter 执行 · Esc 关闭"), "muted"));
        QList<std::pair<QString, std::function<void()>>> commands{
            {newTask->text(), [=] { newTask->click(); }},
            {QStringLiteral("暂停 / 继续派发"), [=] { pause->click(); }}};
        for (int i = 0; i < policy->count(); ++i)
            commands.emplaceBack(QStringLiteral("调度策略 · ") + policy->itemText(i), [=] { policy->setCurrentIndex(i); });
        commands.emplaceBack(QStringLiteral("导出队列报表"), [=] { report->click(); });
        commands.emplaceBack(QStringLiteral("搜索任务记录"), focusSearch);
        for (int i = 0; i < names.size(); ++i) commands.emplaceBack(QStringLiteral("前往 · ") + names[i], [=] { stack->setCurrentIndex(i); });
        if (student) commands.emplaceBack(QStringLiteral("打开学生端"), [=] { student->click(); });
        commands.emplaceBack(QStringLiteral("切换全屏"), toggleFullscreen);
        for(int i=0;i<commands.size();++i){auto* item=new QListWidgetItem(commands[i].first,choices);item->setData(Qt::UserRole,i);}
        choices->setCurrentRow(0);
        connect(query,&QLineEdit::textChanged,&palette,[=](const QString& text){
            QListWidgetItem* first=nullptr;
            for(int i=0;i<choices->count();++i){auto* item=choices->item(i);item->setHidden(!item->text().contains(text,Qt::CaseInsensitive));if(!item->isHidden()&&!first)first=item;}
            choices->setCurrentItem(first);
        });
        int selected=-1;
        auto activate=[&]{auto* item=choices->currentItem();if(item&&!item->isHidden()){selected=item->data(Qt::UserRole).toInt();palette.accept();}};
        connect(query,&QLineEdit::returnPressed,&palette,activate);
        connect(choices,&QListWidget::itemActivated,&palette,[&](QListWidgetItem*){activate();});
        query->setFocus(); if(palette.exec()!=QDialog::Accepted)return;
        if(selected>=0&&selected<commands.size())commands[selected].second();
    };
    connect(command,&QPushButton::clicked,this,showCommands);
    auto* commandKey=new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")),this);
    connect(commandKey,&QShortcut::activated,this,showCommands);
    backendThread_.start();
}
PrintWindow::~PrintWindow() { backendThread_.quit(); backendThread_.wait(); }
void PrintWindow::loadDemo() {
    if (findChild<QComboBox*>("orderPaper")->isEnabled()) {
        QJsonArray jobs;
        for (int i = 0; i < 24; ++i) jobs.append(QJsonObject{
            {"job_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"title", QStringList{QStringLiteral("毕业论文"), QStringLiteral("社团彩页"), QStringLiteral("A3 海报"), QStringLiteral("课堂讲义")}[i % 4] + QString::number(i + 1)},
            {"pages", 8 + (i * 17) % 80}, {"copies", 1 + i % 2}, {"paper", i % 4 == 2 ? "a3" : "a4"},
            {"color", i % 4 == 1 || i % 4 == 2}, {"duplex", i % 4 == 0},
            {"service", QStringList{"express", "standard", "flexible"}[i % 3]}});
        emit shopRequested("submit_batch", {{"jobs", jobs}}); return;
    }
    const QStringList names{QStringLiteral("操作系统实验报告"), QStringLiteral("课程设计说明书"),
        QStringLiteral("复习笔记"), QStringLiteral("实验数据"), QStringLiteral("阅读材料")};
    const int pages[]{80, 60, 32, 18, 12};
    for (int i = 0; i < 5; ++i) emit submitRequested(names[i], pages[i], 1, i == 4 ? 8 : 0);
}
void PrintWindow::importFile(const QString& path) { emit importRequested(path); }
void PrintWindow::applyView(const ViewState& view) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    model_->update(view.jobs);
    scene_->setView(view);
    timeline_->setView(view);
    const bool available = view.connected;
    if (available) emit backendReady();
    demoAvailable_ = view.demoAvailable;
    const bool shopEnabled = view.shop.value("shop_enabled").toBool();
    shopReady_ = available && shopEnabled;
    auto* policy = findChild<QComboBox*>("schedulingPolicy");
    auto* pause = findChild<QPushButton*>("pauseDispatch");
    const QSignalBlocker policyBlocker(policy), pauseBlocker(pause);
    policy->setCurrentIndex(view.policyIndex);
    pause->setChecked(view.paused);
    pause->setText(view.paused ? QStringLiteral("继续派发") : QStringLiteral("暂停派发"));
    pause->setIcon(ui::icon(view.paused ? ui::Icon::Play : ui::Icon::Pause));
    policy->setEnabled(available); pause->setEnabled(available);
    findChild<QPushButton*>("submitJob")->setEnabled(available);
    findChild<QPushButton*>("importJobs")->setEnabled(available);
    connection_->setText(available ? (view.paused ? QStringLiteral("派发已暂停") : QStringLiteral("执行器就绪")) : QStringLiteral("执行器不可用"));
    connection_->setToolTip(view.connection);
    tone(connection_, !available ? "error" : view.paused ? "waiting" : "ready");
    shopSnapshot_ = view.shop;
    for (const auto* field : {"orderPaper", "orderColor", "orderDuplex", "orderService"})
        findChild<QWidget*>(field)->setEnabled(shopReady_);
    const auto fleet = view.shop.value("fleet").toArray();
    const int count = int(view.printers.size());
    if (fleetTable_->rowCount() != count) { fleetTable_->setRowCount(count); ui::fitRows(fleetTable_); }
    int busy = 0, offline = 0;
    for (int i = 0; i < count; ++i) {
        const auto object = fleet[i].toObject();
        const auto& printer = view.printers[i];
        const bool idle = printer.jobId.isEmpty(), online = object.value("online").toBool(true);
        if (!idle) ++busy;
        if (!online) ++offline;
        const QList<ui::Cell> cells{{text::deviceName(object, i), "", -1, "strong"}, text::capability(object),
            object.isEmpty() ? QString::number(view.shop.value("page_ms").toDouble() / 1000, 'f', 2)
                : QStringLiteral("%1 / %2").arg(object.value("mono_page_ms").toDouble() / 1000, 0, 'f', 2).arg(object.value("color_page_ms").toDouble() / 1000, 0, 'f', 2),
            {online ? QStringLiteral("接单中") : QStringLiteral("暂停接单"), online ? "ready" : "waiting"},
            {idle ? QStringLiteral("空闲") : printer.title, "", -1, idle ? "muted" : ""},
            idle ? ui::Cell(QStringLiteral("—"), "", -1, "muted")
                 : ui::Cell(QStringLiteral("%1 / %2 面").arg(printer.printed).arg(printer.total), "", printer.total > 0 ? double(printer.printed) / printer.total : 0.)};
        for (int c = 0; c < cells.size(); ++c) ui::setCell(fleetTable_, i, c, cells[c]);
        fleetTable_->item(i, 0)->setData(Qt::UserRole, idle);
    }
    fleetSummary_->setText(QStringLiteral("%1 台设备 · %2 台打印中").arg(count).arg(busy) +
        (offline > 0 ? QStringLiteral(" · %1 台暂停接单").arg(offline) : QString()));
    const auto business = view.shop.value("shop_metrics").toObject();
    const auto paid = business.value("paid_cents").toObject();
    const auto amount = [shopEnabled](const QJsonObject& source, const char* key) {
        return shopEnabled ? text::money(Money(source.value(QLatin1String(key)).toDouble())) : QStringLiteral("—");
    };
    const std::array<const char*, 4> ledgerKeys{"cost_cents", "contribution_cents", "refund_cents", "pending_cents"};
    const std::array<const char*, 4> channelKeys{"wechat", "alipay", "campus_card", "counter"};
    const std::array<const char*, 4> orderKeys{"completed", "on_time", "late", "overdue"};
    for (int i = 0; i < 4; ++i) {
        ledger_[i]->setText(amount(business, ledgerKeys[i]));
        ledger_[i + 4]->setText(amount(paid, channelKeys[i]));
        ledger_[i + 8]->setText(shopEnabled ? QStringLiteral("%1 单").arg(business.value(QLatin1String(orderKeys[i])).toInt()) : QStringLiteral("—"));
    }
    tone(ledger_[10], business.value("late").toInt() > 0 ? "waiting" : "");
    tone(ledger_[11], business.value("overdue").toInt() > 0 ? "error" : "");
    shopSummary_->setVisible(!shopEnabled);
    if (available && !shopEnabled)
        shopSummary_->setText(QStringLiteral("此目录是旧版队列。使用新的数据目录启动经营模式即可启用六台设备与订单计价。"));
    const int blockedOrders = view.shop.value("blocked_orders").toInt();
    const bool pending = view.metrics.queued + view.metrics.running + view.metrics.cancelling > 0;
    const auto clearing = view.queueClearMs < 0 ? (pending ? QStringLiteral("待完成订单暂无法预计") : QStringLiteral("当前没有待完成订单"))
        : view.paused ? QStringLiteral("派发已暂停 · 恢复后%1清空当前队列").arg(text::duration(view.queueClearMs - view.nowMs))
        : QStringLiteral("预计 %1 清空当前队列（%2）").arg(text::clock(view.queueClearMs), text::duration(view.queueClearMs - view.nowMs));
    shopValues_[0]->setText(amount(business, "revenue_cents"));
    shopHints_[0]->setText(QStringLiteral("贡献利润 %1 · 完成 %2 单").arg(text::money(Money(business.value("contribution_cents").toDouble()))).arg(business.value("completed").toInt()));
    shopValues_[1]->setText(amount(business, "prepaid_cents"));
    shopHints_[1]->setText(QStringLiteral("已支付、未完成 · 待完成订单共 %1").arg(text::money(Money(business.value("pending_cents").toDouble()))));
    shopValues_[2]->setText(QStringLiteral("%1 单").arg(static_cast<qulonglong>(view.metrics.queued)));
    shopHints_[2]->setText(clearing);
    shopValues_[3]->setText(QStringLiteral("%1 / %2 台打印中").arg(busy).arg(count));
    shopHints_[3]->setText(QStringLiteral("A4 %1 张 · A3 %2 张").arg(view.shop.value("stock_a4").toInt()).arg(view.shop.value("stock_a3").toInt()) +
        (offline > 0 ? QStringLiteral(" · %1 台暂停接单").arg(offline) : QString()));
    shopForecast_->setText(clearing + (blockedOrders > 0 ? QStringLiteral("\n%1 单因缺纸或设备暂停暂无法预计").arg(blockedOrders) : QString()));
    tone(shopForecast_, blockedOrders > 0 || (view.paused && pending) ? "waiting" : "");
    const std::array<const char*, 2> stockKeys{"stock_a4", "stock_a3"};
    for (int i = 0; i < 2; ++i) {
        const int sheets = view.shop.value(QLatin1String(stockKeys[i])).toInt();
        stock_[i]->setText(shopEnabled ? QStringLiteral("%1 张").arg(sheets) : QStringLiteral("—"));
        tone(stock_[i], shopEnabled && sheets == 0 ? "error" : "");
    }
    for (const auto* name : {"restock_a4", "restock_a3"}) findChild<QPushButton*>(name)->setEnabled(shopReady_);
    const auto& m = view.metrics;
    metricValues_[0]->setText(QString::number(static_cast<qulonglong>(m.completed)));
    metricValues_[1]->setText(QString::number(static_cast<qulonglong>(m.queued)));
    metricValues_[2]->setText(QStringLiteral("%1 秒").arg(m.averageFirstWaitMs / 1000, 0, 'f', 1));
    metricValues_[3]->setText(QStringLiteral("%1%").arg(m.utilization * 100, 0, 'f', 1));
    metricHints_[0]->setText(QStringLiteral("失败 %1 · 已取消 %2").arg(static_cast<qulonglong>(m.failed)).arg(static_cast<qulonglong>(m.cancelled)));
    metricHints_[1]->setText(QStringLiteral("%1 个任务正在执行").arg(static_cast<qulonglong>(m.running + m.cancelling)));
    metricHints_[2]->setText(QStringLiteral("成功任务平均周转 %1 秒").arg(m.averageTurnaroundMs / 1000, 0, 'f', 1));
    metricHints_[3]->setText(QStringLiteral("吞吐量 %1 任务 / 分钟").arg(m.throughputPerSecond * 60, 0, 'f', 1));
    chart_->updateMetrics(m);
    updateSelection();
    updateDeviceActions();
}
void PrintWindow::updateSelection() {
    const auto current = table_->currentIndex();
    const int state = current.isValid() ? current.data(Qt::UserRole + 1).toInt() : -1;
    const bool queued = state == static_cast<int>(JobState::Queued);
    const bool running = state == static_cast<int>(JobState::Running);
    const bool connected = current.data(Qt::UserRole + 2).toBool();
    if (current.isValid() && table_->isVisible()) scene_->select(current.data(Qt::UserRole).toString());
    cancel_->setEnabled((queued || running) && connected);
    cancel_->setToolTip(queued ? QStringLiteral("立即取消尚未领取的任务") : QStringLiteral("请求停止正在执行的任务，收到停止确认后生效。"));
    const bool refundable = state == static_cast<int>(JobState::Completed) &&
        current.data(Qt::UserRole + 6).toDouble() < current.data(Qt::UserRole + 5).toDouble();
    refund_->setEnabled(shopReady_ && refundable);
    refund_->setToolTip(refundable ? QStringLiteral("为选中的已完成订单全额退款（模拟）") : QStringLiteral("选中一笔尚未退款的已完成订单后可退款"));
    if (fault_) fault_->setEnabled(running && connected && demoAvailable_);
    if (disconnect_) disconnect_->setEnabled(running && connected);
    const int shown = filter_->rowCount(), total = model_->rowCount();
    queueSummary_->setText(shown == total ? QStringLiteral("%1 个任务").arg(total) : QStringLiteral("显示 %1 / %2 个任务").arg(shown).arg(total));
    emptyState_->setVisible(shown == 0);
}
void PrintWindow::updateDeviceActions() {
    const int row = fleetTable_->currentRow();
    const auto fleet = shopSnapshot_.value("fleet").toArray();
    const bool chosen = shopReady_ && row >= 0 && row < fleet.size() && fleetTable_->item(row, 0);
    const bool online = chosen && fleet[row].toObject().value("online").toBool(true);
    const bool idle = chosen && fleetTable_->item(row, 0)->data(Qt::UserRole).toBool();
    toggleDevice_->setEnabled(chosen);
    toggleDevice_->setText(chosen && !online ? QStringLiteral("恢复接单") : QStringLiteral("暂停接单"));
    toggleDevice_->setToolTip(!chosen ? QStringLiteral("先在列表中选中一台设备") : online ? QStringLiteral("不再分配新订单；正在打印的订单会继续完成。")
        : QStringLiteral("恢复后重新参与派发。"));
    editDevice_->setEnabled(chosen && idle);
    editDevice_->setToolTip(!chosen ? QStringLiteral("先在列表中选中一台设备") : idle ? QStringLiteral("修改名称、能力和速度")
        : QStringLiteral("设备正在打印，空闲后才能编辑。"));
}
// Every other column has a fixed width, so the document column absorbs the rest.
void PrintWindow::fitTitleColumn() {
    int others = 0;
    for (int column = 1; column < model_->columnCount(); ++column)
        if (!table_->isColumnHidden(column)) others += table_->columnWidth(column);
    const int width = std::max(220, table_->viewport()->width() - others);
    if (table_->columnWidth(0) != width) table_->setColumnWidth(0, width);
}
bool PrintWindow::eventFilter(QObject* watched, QEvent* event) {
    if (table_ && watched == table_->viewport() && event->type() == QEvent::Resize) fitTitleColumn();
    return QMainWindow::eventFilter(watched, event);
}
void PrintWindow::closeEvent(QCloseEvent* event) {
    if (closeApproved_) { event->accept(); return; }
    event->ignore();
    if (closePending_) return;
    closePending_ = true; centralWidget()->setEnabled(false); emit closeRequested();
}
} // namespace cps::gui
