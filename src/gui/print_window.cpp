#include "print_window.hpp"
#include "ui_theme.hpp"
#include "paper_workspace.hpp"
#include <QStackedWidget>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
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

QLabel* label(const QString& text, const char* role = nullptr) {
    auto* result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (role) result->setProperty("role", role);
    if (role && QLatin1String(role) == QLatin1String("badge")) {
        result->setFixedHeight(28);
        result->setAlignment(Qt::AlignCenter);
    }
    return result;
}

void chevron(QPainter& p, const QPointF& center, bool up = false) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#718095"), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double direction = up ? -1 : 1;
    QPainterPath path;
    path.moveTo(center.x() - 3, center.y() - direction * 1.5);
    path.lineTo(center.x(), center.y() + direction * 1.5);
    path.lineTo(center.x() + 3, center.y() - direction * 1.5);
    p.drawPath(path);
}

class SelectInput : public QComboBox {
public:
    using QComboBox::QComboBox;
protected:
    void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        chevron(painter, QPointF(width() - 15, height() / 2.0));
    }
};

class NumberInput : public QSpinBox {
public:
    NumberInput() { setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); }
protected:
    void paintEvent(QPaintEvent* event) override {
        QSpinBox::paintEvent(event);
        QPainter painter(this);
        chevron(painter, QPointF(width() - 11, height() / 4.0 + 1), true);
        chevron(painter, QPointF(width() - 11, height() * 3 / 4.0 - 1));
    }
};

QFrame* card(const char* role = "card") {
    auto* result = new QFrame;
    result->setProperty("role", role);
    return result;
}

void tone(QWidget* widget, const char* value) {
    if (widget->property("tone").toString() == QLatin1String(value)) return;
    widget->setProperty("tone", value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// Draw the visible cells only, retaining the existing model and selection semantics.
class QueueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        painter->fillRect(option.rect, QColor(selected ? "#EAF0FC" : hovered ? "#F6F8FC" : "#FFFFFF"));
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
            painter->drawText(name.translated(0, 20), Qt::AlignVCenter, "# " + index.data(Qt::UserRole).toString().left(8));
        } else if (index.column() == 3) {
            const auto state = static_cast<JobState>(index.data(Qt::UserRole + 1).toInt());
            const bool success = state == JobState::Completed;
            const bool failed = state == JobState::Failed;
            const bool running = state == JobState::Running;
            const bool cancelling = state == JobState::Cancelling;
            const QColor ink(success ? "#3F8D7E" : failed ? "#BF655C" : running ? "#3569ED" : cancelling ? "#A77D43" : "#78869B");
            const QColor fill(success ? "#EAF5F1" : failed ? "#FBEDEA" : running ? "#EAF0FE" : cancelling ? "#FAF2E4" : "#F0F3F8");
            const QString text = index.data().toString();
            const int width = std::min(area.width(), painter->fontMetrics().horizontalAdvance(text) + 18);
            painter->setPen(Qt::NoPen); painter->setBrush(fill);
            const QRect pill(area.left(), area.center().y() - 12, width, 24);
            painter->drawRoundedRect(pill, 6, 6); painter->setPen(ink);
            painter->drawText(pill, Qt::AlignCenter, text);
        } else if (index.column() == 4) {
            const int printed = index.data(Qt::UserRole + 3).toInt();
            const int sheets = index.data(Qt::UserRole + 4).toInt();
            painter->setPen(QColor("#718095"));
            painter->drawText(QRect(area.left(), area.top() + 10, area.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QStringLiteral("%1 / %2 页").arg(printed).arg(sheets));
            const QRectF track(area.left(), area.bottom() - 15, area.width(), 4);
            painter->setPen(Qt::NoPen); painter->setBrush(QColor("#E4E9F2")); painter->drawRoundedRect(track, 2, 2);
            if (sheets > 0 && printed > 0) {
                painter->setBrush(QColor("#3569ED"));
                painter->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * printed / sheets, 4), 2, 2);
            }
        } else {
            painter->setPen(QColor(index.column() == 5 ? "#718095" : "#59667C"));
            const auto value = painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, area.width());
            painter->drawText(area, Qt::AlignLeft | Qt::AlignVCenter, value);
        }
        painter->restore();
    }
};
}
int JobTableModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(jobs_.size()); }
int JobTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 7; }
QVariant JobTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= jobs_.size()) return {};
    const auto& job = jobs_[index.row()];
    if (role == Qt::UserRole) return job.id;
    if (role == Qt::UserRole + 1) return static_cast<int>(job.state);
    if (role == Qt::UserRole + 2) return job.controlReady;
    if (role == Qt::UserRole + 3) return job.printed;
    if (role == Qt::UserRole + 4) return job.sheets;
    if (role == Qt::ToolTipRole) return job.title + "\n" + job.id + (job.detail.isEmpty() ? QString() : "\n" + job.detail);
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
    const std::array<QColor, 5> colors{QColor("#A4B8DB"), QColor("#3569ED"), QColor("#66A89B"), QColor("#D89079"), QColor("#B0B8C8")};
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
        p.setPen(QColor("#718095"));
        p.drawText(QRect(left + 12, top, column - 12, 18), Qt::AlignVCenter,
                   labels[i] + "  " + QString::number(static_cast<qulonglong>(values[i])));
    }
}

PrintWindow::PrintWindow(WindowOptions options, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("校园打印 · Paper"));
    setMinimumSize(1000, 680);
    const auto available = screen()->availableGeometry();
    resize(std::min(1420, available.width() - 40), std::min(900, available.height() - 60));
    QFont interfaceFont = font();
    const auto families = QFontDatabase::families();
    const QStringList candidates{
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei")
    };
    for (const auto& family : candidates) {
        if (!families.contains(family)) continue;
        QFont candidate = interfaceFont;
        candidate.setFamily(family);
        if (!QFontMetrics(candidate).inFontUcs4(0x6821)) continue;
        interfaceFont = candidate;
        break;
    }
    interfaceFont.setPixelSize(13);
    QApplication::setFont(interfaceFont);
    setFont(interfaceFont);
    QPalette light = palette();
    light.setColor(QPalette::Window, QColor("#F7F8FB"));
    light.setColor(QPalette::WindowText, QColor("#202737"));
    light.setColor(QPalette::Base, Qt::white); light.setColor(QPalette::Text, QColor("#293449"));
    light.setColor(QPalette::Button, Qt::white); light.setColor(QPalette::ButtonText, QColor("#59667C"));
    light.setColor(QPalette::Highlight, QColor("#DCE7FE")); light.setColor(QPalette::HighlightedText, QColor("#234B9D"));
    setPalette(light);
    setStyleSheet(ui::styleSheet());
    auto* central = new QWidget(this); central->setObjectName("workspace");
    auto* layout = new QVBoxLayout(central); layout->setContentsMargins(24, 10, 24, 12); layout->setSpacing(12);
    auto* header = new QHBoxLayout; header->setSpacing(10);
    auto* mark = new QLabel; mark->setPixmap(ui::icon(ui::Icon::Document, QColor("#26354D")).pixmap(30, 30)); header->addWidget(mark);
    auto* brand = label(QStringLiteral("校园打印")); brand->setObjectName("pageTitle"); header->addWidget(brand); header->addSpacing(24);
    auto* navigation = new QButtonGroup(this); navigation->setExclusive(true);
    auto* stack = new QStackedWidget; stack->setObjectName("workspacePages");
    const QStringList names{QStringLiteral("工作台"), QStringLiteral("记录"), QStringLiteral("设备")};
    const QStringList navIds{"navWorkspace", "navHistory", "navDevices"};
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(names[i]); button->setObjectName(navIds[i]); button->setProperty("role", "nav");
        button->setCheckable(true); button->setChecked(i == 0); navigation->addButton(button, i); header->addWidget(button);
        connect(button, &QPushButton::clicked, this, [stack, i] { stack->setCurrentIndex(i); });
    }
    connect(stack, &QStackedWidget::currentChanged, this, [navigation](int index) { navigation->button(index)->setChecked(true); });
    header->addStretch();
    auto* command = new QPushButton(QStringLiteral("⌘  命令")); command->setObjectName("commandPalette"); command->setProperty("role", "quiet");
    command->setText(QStringLiteral("命令  Ctrl+K")); header->addWidget(command);
    if (options.demoMode) {
        auto* demo = new QPushButton(QStringLiteral("演示任务")); demo->setObjectName("demoWorkload"); demo->setProperty("role", "quiet");
        header->addWidget(demo); connect(demo, &QPushButton::clicked, this, &PrintWindow::loadDemo);
    }
    auto* newTask = new QPushButton(QStringLiteral("新建任务")); newTask->setObjectName("newTask"); newTask->setProperty("role", "primary");
    newTask->setIcon(ui::icon(ui::Icon::Plus, Qt::white)); header->addWidget(newTask); layout->addLayout(header);

    // Workbench: the illustration and queue share one clean surface; controls are native.
    auto* workArea = new QScrollArea; workArea->setObjectName("workbenchScroll");
    workArea->setWidgetResizable(true); workArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* work = new QWidget; auto* body = new QVBoxLayout(work);
    body->setContentsMargins(0, 0, 0, 0); body->setSpacing(0); body->setSizeConstraint(QLayout::SetMinimumSize);
    scene_ = new PaperWorkspace; body->addWidget(scene_, 1);
    auto* controls = new QFrame; controls->setObjectName("timelineControls");
    controls->setStyleSheet("QFrame#timelineControls { background: white; border-top: 1px solid #E5EAF2; }");
    auto* controlRow = new QHBoxLayout(controls); controlRow->setContentsMargins(28, 17, 28, 10);
    controlRow->addWidget(label(QStringLiteral("设备与调度"), "section")); controlRow->addStretch();
    auto* policy = new SelectInput; policy->setObjectName("schedulingPolicy");
    policy->addItems({QStringLiteral("FCFS · 先来先服务"), QStringLiteral("SJF · 短作业优先"), QStringLiteral("优先级 + 等待老化")});
    policy->setMinimumWidth(190); controlRow->addWidget(policy); controlRow->addStretch();
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
    auto* queue = card(); auto* queueLayout = new QVBoxLayout(queue); queueLayout->setContentsMargins(18, 16, 18, 14);
    auto* actions = new QHBoxLayout; actions->addWidget(label(QStringLiteral("任务记录"), "section"));
    queueSummary_ = label(QStringLiteral("0 个任务"), "muted"); actions->addWidget(queueSummary_); actions->addStretch();
    auto* filter = new SelectInput; filter->setObjectName("queueFilter");
    filter->addItems({QStringLiteral("全部任务"), QStringLiteral("等待中"), QStringLiteral("打印中"), QStringLiteral("已完成"), QStringLiteral("失败"), QStringLiteral("已取消")});
    actions->addWidget(filter);
    auto* report = new QPushButton(QStringLiteral("导出报表")); report->setIcon(ui::icon(ui::Icon::Export)); actions->addWidget(report);
    cancel_ = new QPushButton(QStringLiteral("取消任务")); cancel_->setObjectName("cancelJob"); cancel_->setProperty("role", "danger"); cancel_->setEnabled(false); actions->addWidget(cancel_);
    queueLayout->addLayout(actions);
    model_ = new JobTableModel(this); filter_ = new QSortFilterProxyModel(this); filter_->setSourceModel(model_); filter_->setFilterKeyColumn(3);
    table_ = new QTableView; table_->setObjectName("jobsTable"); table_->setModel(filter_); table_->setItemDelegate(new QueueDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setShowGrid(false); table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight); table_->setMinimumHeight(220);
    table_->verticalHeader()->setVisible(false); table_->verticalHeader()->setDefaultSectionSize(58);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed); table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 65); table_->setColumnWidth(2, 65); table_->setColumnWidth(3, 90);
    table_->setColumnWidth(4, 130); table_->setColumnWidth(5, 220); table_->hideColumn(6); queueLayout->addWidget(table_, 1);
    auto* overlay = new QVBoxLayout(table_->viewport()); overlay->setContentsMargins(0, 0, 0, 0);
    emptyState_ = new QWidget; emptyState_->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* emptyLayout = new QVBoxLayout(emptyState_); emptyLayout->addStretch();
    auto* emptyTitle = label(QStringLiteral("这里暂时没有任务"), "section"); emptyTitle->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyTitle);
    auto* emptyHint = label(QStringLiteral("新建任务，或切换状态筛选"), "muted"); emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyHint); emptyLayout->addStretch(); overlay->addWidget(emptyState_);
    if (options.demoMode) {
        auto* demo = new QHBoxLayout; demo->addWidget(label(QStringLiteral("故障恢复演示"), "muted")); demo->addStretch();
        fault_ = new QPushButton(QStringLiteral("模拟进程崩溃")); fault_->setObjectName("injectFault"); fault_->setEnabled(false); demo->addWidget(fault_);
        disconnect_ = new QPushButton(QStringLiteral("断开控制连接")); disconnect_->setObjectName("disconnectControl"); disconnect_->setEnabled(false); demo->addWidget(disconnect_);
        queueLayout->addLayout(demo);
    }
    working->addWidget(queue, 1); queueArea->setWidget(queueContent); stack->addWidget(queueArea);

    auto* devices = new QWidget; auto* deviceLayout = new QVBoxLayout(devices); deviceLayout->setContentsMargins(0, 12, 0, 0); deviceLayout->setSpacing(18);
    auto* deviceHead = new QHBoxLayout; deviceHead->addWidget(label(QStringLiteral("设备与执行状态"), "section")); deviceHead->addStretch();
    auto* check = new QPushButton(QStringLiteral("检查执行器")); check->setIcon(ui::icon(ui::Icon::Refresh)); deviceHead->addWidget(check);
    auto* folder = new QPushButton(QStringLiteral("打开输出目录")); folder->setIcon(ui::icon(ui::Icon::Folder)); deviceHead->addWidget(folder);
    deviceLayout->addLayout(deviceHead);
    connect(folder, &QPushButton::clicked, this, [this, options] {
        const auto output = QDir(options.stateDirectory).filePath("output");
        if (!QDir().mkpath(output) || !QDesktopServices::openUrl(QUrl::fromLocalFile(output))) feedback_->setText(QStringLiteral("无法打开输出目录。"));
    });
    auto* printers = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        auto* box = card(); auto* d = new QVBoxLayout(box); d->setContentsMargins(25, 24, 25, 24); d->setSpacing(15);
        auto* top = new QHBoxLayout; top->addWidget(label(QStringLiteral("打印机 0%1").arg(i + 1), "section")); top->addStretch();
        printerStates_[i] = label(QStringLiteral("就绪"), "badge"); top->addWidget(printerStates_[i]); d->addLayout(top);
        printerLabels_[i] = label(QStringLiteral("等待下一个任务")); printerLabels_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); d->addWidget(printerLabels_[i]);
        printerDetails_[i] = label({}, "muted"); printerDetails_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); d->addWidget(printerDetails_[i]);
        progressBars_[i] = new QProgressBar; progressBars_[i]->setTextVisible(false); progressBars_[i]->setValue(0); d->addWidget(progressBars_[i]);
        printerProgress_[i] = label({}, "muted"); d->addWidget(printerProgress_[i]); printers->addWidget(box);
    }
    deviceLayout->addLayout(printers);
    auto* distribution = card(); auto* dist = new QVBoxLayout(distribution); dist->setContentsMargins(25, 20, 25, 14);
    dist->addWidget(label(QStringLiteral("本次任务分布"), "section")); chart_ = new StatisticsChart; dist->addWidget(chart_); deviceLayout->addWidget(distribution);
    auto* note = label(QStringLiteral("当前使用本地模拟打印，两台独立工作进程向文本文件输出。\n队列由独立后台保存；关闭窗口后任务继续执行，重开可查看最新结果。"), "muted");
    note->setWordWrap(true); deviceLayout->addWidget(note); deviceLayout->addStretch(); stack->addWidget(devices);
    layout->addWidget(stack, 1);
    auto* footer = new QHBoxLayout; footer->setSpacing(12);
    connection_ = label(QStringLiteral("检查执行器…"), "badge"); footer->addWidget(connection_);
    feedback_ = label(QStringLiteral("新建一份任务，或加载演示任务体验双设备调度。"), "muted");
    feedback_->setObjectName("feedback"); feedback_->setWordWrap(true); footer->addWidget(feedback_, 1);
    logToggle_ = new QToolButton; logToggle_->setObjectName("logToggle"); logToggle_->setCheckable(true); logToggle_->setProperty("role", "quiet");
    logToggle_->setText(QStringLiteral("运行日志")); footer->addWidget(logToggle_); layout->addLayout(footer);
    log_ = new QPlainTextEdit; log_->setReadOnly(true); log_->setMaximumBlockCount(500); log_->setFixedHeight(120); log_->hide();
    layout->addWidget(log_); connect(logToggle_, &QToolButton::toggled, log_, &QWidget::setVisible); setCentralWidget(central);

    auto* submission = new QDialog(this); submission->setObjectName("submissionDialog"); submission->setWindowTitle(QStringLiteral("新建打印任务"));
    submission->setWindowModality(Qt::WindowModal); submission->setFixedWidth(470);
    auto* form = new QVBoxLayout(submission); form->setContentsMargins(28, 26, 28, 26); form->setSpacing(13);
    form->addWidget(label(QStringLiteral("准备打印什么？"), "section")); form->addWidget(label(QStringLiteral("输入文档信息，交给调度器安排。"), "muted"));
    form->addWidget(label(QStringLiteral("文档名称"), "muted"));
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
    auto* sheets = label(QStringLiteral("本次打印共 8 页"), "muted"); sheets->setWordWrap(true); form->addWidget(sheets);
    auto sheetSummary = [=] { sheets->setText(QStringLiteral("本次打印共 %1 页 · 优先级越大越优先").arg(pages->value() * copies->value())); };
    connect(pages, &QSpinBox::valueChanged, this, sheetSummary); connect(copies, &QSpinBox::valueChanged, this, sheetSummary);
    auto* submit = new QPushButton(QStringLiteral("加入打印队列")); submit->setObjectName("submitJob"); submit->setProperty("role", "primary"); submit->setDefault(true); form->addWidget(submit);
    auto* import = new QPushButton(QStringLiteral("从 JSON 文件批量导入")); import->setObjectName("importJobs"); form->addWidget(import);
    connect(newTask, &QPushButton::clicked, this, [=] { submission->show(); submission->raise(); title->setFocus(); title->selectAll(); });
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
        if (title->text().trimmed().isEmpty() || pages->value() * copies->value() > 10000) {
            const auto error = QStringLiteral("请输入有效名称，总页数不得超过 10000。");
            sheets->setText(error); feedback_->setText(error); return;
        }
        emit submitRequested(title->text(), pages->value(), copies->value(), priority->value());
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
    connect(filter, &QComboBox::currentIndexChanged, this, [this, filter](int index) {
        filter_->setFilterFixedString(index == 0 ? QString() : filter->currentText());
        emptyState_->setVisible(filter_->rowCount() == 0);
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
        palette.setMinimumWidth(480); palette.resize(520, 420);
        auto* body = new QVBoxLayout(&palette); body->setContentsMargins(20,20,20,20); body->setSpacing(12);
        body->addWidget(label(QStringLiteral("你想做什么？"), "section"));
        auto* query = new QLineEdit; query->setObjectName("commandQuery"); query->setPlaceholderText(QStringLiteral("输入命令关键词，Enter 执行")); body->addWidget(query);
        auto* choices = new QListWidget; choices->setObjectName("commandChoices"); body->addWidget(choices);
        const QStringList commands{QStringLiteral("新建打印任务"), QStringLiteral("暂停 / 继续派发"),QStringLiteral("调度策略 · FCFS 先来先服务"),
            QStringLiteral("调度策略 · SJF 短作业优先"),QStringLiteral("调度策略 · 优先级老化"),QStringLiteral("导出队列报表"),QStringLiteral("切换全屏"),QStringLiteral("回到工作台")};
        for(int i=0;i<commands.size();++i){auto* item=new QListWidgetItem(commands[i],choices);item->setData(Qt::UserRole,i);}
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
        if(selected==0)newTask->click();
        else if(selected==1)pause->click();
        else if(selected>=2&&selected<=4)policy->setCurrentIndex(selected-2);
        else if(selected==5)report->click();
        else if(selected==6)toggleFullscreen();
        else if(selected==7)stack->setCurrentIndex(0);
    };
    connect(command,&QPushButton::clicked,this,showCommands);
    auto* commandKey=new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")),this);
    connect(commandKey,&QShortcut::activated,this,showCommands);
    backendThread_.start();
}
PrintWindow::~PrintWindow() { backendThread_.quit(); backendThread_.wait(); }
void PrintWindow::loadDemo() {
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
    emptyState_->setVisible(filter_->rowCount() == 0);
    queueSummary_->setText(QStringLiteral("%1 个任务").arg(view.jobs.size()));
    const bool available = view.connected;
    demoAvailable_ = view.demoAvailable;
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
    for (int i = 0; i < 2; ++i) {
        const auto& printer = view.printers[i];
        const bool active = !printer.jobId.isEmpty();
        const QString name = active ? printer.title : QStringLiteral("等待下一个任务");
        printerLabels_[i]->setText(printerLabels_[i]->fontMetrics().elidedText(name, Qt::ElideRight, printerLabels_[i]->width()));
        printerLabels_[i]->setToolTip(printer.title);
        const QString detail = active ? printer.status + (printer.connection.isEmpty() ? QString() : " · " + printer.connection)
                                      : QStringLiteral("提交后将自动分配任务");
        printerDetails_[i]->setText(printerDetails_[i]->fontMetrics().elidedText(detail, Qt::ElideRight, printerDetails_[i]->width()));
        printerDetails_[i]->setToolTip(detail);
        printerStates_[i]->setText(active ? QStringLiteral("工作中") : QStringLiteral("就绪"));
        tone(printerStates_[i], active ? "active" : "ready");
        progressBars_[i]->setRange(0, printer.total > 0 ? printer.total : 100);
        progressBars_[i]->setValue(printer.printed);
        printerProgress_[i]->setText(active ? QStringLiteral("%1 / %2 页 · %3% 已完成")
            .arg(printer.printed).arg(printer.total).arg(printer.total > 0 ? printer.printed * 100 / printer.total : 0)
            : QStringLiteral("当前没有运行任务"));
    }
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
}
void PrintWindow::updateSelection() {
    const auto current = table_->currentIndex();
    const bool queued = current.isValid() && current.data(Qt::UserRole + 1).toInt() == static_cast<int>(JobState::Queued);
    const bool running = current.isValid() && current.data(Qt::UserRole + 1).toInt() == static_cast<int>(JobState::Running);
    const bool connected = current.data(Qt::UserRole + 2).toBool();
    if (current.isValid() && table_->isVisible()) scene_->select(current.data(Qt::UserRole).toString());
    cancel_->setEnabled((queued || running) && connected);
    cancel_->setToolTip(queued ? QStringLiteral("立即取消尚未领取的任务") : QStringLiteral("请求停止正在执行的任务，收到停止确认后生效。"));
    if (fault_) fault_->setEnabled(running && connected && demoAvailable_);
    if (disconnect_) disconnect_->setEnabled(running && connected);
}
void PrintWindow::closeEvent(QCloseEvent* event) {
    if (closeApproved_) { event->accept(); return; }
    event->ignore();
    if (closePending_) return;
    closePending_ = true; centralWidget()->setEnabled(false); emit closeRequested();
}
} // namespace cps::gui
