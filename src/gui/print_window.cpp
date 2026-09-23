#include "print_window.hpp"
#include "ui_theme.hpp"
#include "paper_workspace.hpp"
#include "cps/runtime/shop_io.hpp"
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
int JobTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 10; }
QVariant JobTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= jobs_.size()) return {};
    const auto& job = jobs_[index.row()];
    if (role == Qt::UserRole) return job.id;
    if (role == Qt::UserRole + 1) return static_cast<int>(job.state);
    if (role == Qt::UserRole + 2) return job.controlReady;
    if (role == Qt::UserRole + 3) return job.printed;
    if (role == Qt::UserRole + 4) return job.sheets;
    if (role == Qt::UserRole + 5) return double(job.quotedCents);
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
    case 7: return job.specification;
    case 8: return QStringLiteral("¥%1").arg(job.quotedCents / 100.0, 0, 'f', 2);
    case 9: return job.deadline;
    default: return {};
    }
}
QVariant JobTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    const QStringList headings{QStringLiteral("文档"), QStringLiteral("总页数"), QStringLiteral("优先级"),
        QStringLiteral("状态"), QStringLiteral("已打印 / 总页数"), QStringLiteral("结果 / 错误"), QStringLiteral("任务 ID"), QStringLiteral("规格"), QStringLiteral("锁定报价"), QStringLiteral("交付期限")};
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
    setWindowTitle(QStringLiteral("打印店经营与协同调度 · Paper"));
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
    auto* brand = label(QStringLiteral("打印店")); brand->setObjectName("pageTitle"); header->addWidget(brand); header->addSpacing(24);
    auto* navigation = new QButtonGroup(this); navigation->setExclusive(true);
    auto* stack = new QStackedWidget; stack->setObjectName("workspacePages");
    const QStringList names{QStringLiteral("工作台"), QStringLiteral("记录"), QStringLiteral("设备"), QStringLiteral("经营")};
    const QStringList navIds{"navWorkspace", "navHistory", "navDevices", "navShop"};
    for (int i = 0; i < 4; ++i) {
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
    policy->addItems({QStringLiteral("FCFS · 先来先服务"), QStringLiteral("SJF · 短作业优先"), QStringLiteral("优先级 + 等待老化"), QStringLiteral("交付期限优先"), QStringLiteral("营业收入导向")});
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
    table_->setColumnWidth(4, 130); table_->setColumnWidth(5, 140); table_->hideColumn(6); if (options.shopMode) table_->hideColumn(2); table_->setColumnWidth(7, 125); table_->setColumnWidth(8, 95); table_->setColumnWidth(9, 130); queueLayout->addWidget(table_, 1);
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
    fleetTable_ = new QTableWidget(0, 6); fleetTable_->setObjectName("fleetTable");
    fleetTable_->setHorizontalHeaderLabels({QStringLiteral("设备"), QStringLiteral("能力"), QStringLiteral("模拟秒/页"), QStringLiteral("接单状态"), QStringLiteral("当前订单"), QStringLiteral("进度")});
    fleetTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    fleetTable_->setSelectionBehavior(QAbstractItemView::SelectRows); fleetTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    fleetTable_->setEditTriggers(QAbstractItemView::NoEditTriggers); fleetTable_->verticalHeader()->hide();
    deviceLayout->addWidget(fleetTable_, 1);
    auto* deviceActions = new QHBoxLayout;
    auto* toggleDevice = new QPushButton(QStringLiteral("切换所选设备接单状态")); toggleDevice->setObjectName("togglePrinter");
    auto* editDevice = new QPushButton(QStringLiteral("编辑空闲设备")); editDevice->setObjectName("editPrinter");
    deviceActions->addWidget(toggleDevice); deviceActions->addWidget(editDevice); deviceActions->addStretch(); deviceLayout->addLayout(deviceActions);
    connect(toggleDevice, &QPushButton::clicked, this, [this] {
        const int i = fleetTable_->currentRow(); const auto fleet = shopSnapshot_.value("fleet").toArray();
        if (i < 0 || i >= fleet.size()) return;
        emit shopRequested("set_printer", {{"index", i}, {"online", !fleet[i].toObject().value("online").toBool()}});
    });
    connect(editDevice, &QPushButton::clicked, this, [this] {
        const int i = fleetTable_->currentRow(); const auto fleet = shopSnapshot_.value("fleet").toArray();
        if (i < 0 || i >= fleet.size()) return;
        auto object = fleet[i].toObject(); QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("编辑设备 · 仅空闲时生效"));
        auto* fields = new QVBoxLayout(&dialog);
        auto* name = new QLineEdit(object.value("name").toString()); fields->addWidget(label(QStringLiteral("设备名称"))); fields->addWidget(name);
        auto* a3 = new QCheckBox("A3"); a3->setChecked(object.value("a3").toBool()); fields->addWidget(a3);
        auto* color = new QCheckBox(QStringLiteral("彩色")); color->setChecked(object.value("color").toBool()); fields->addWidget(color);
        auto* duplex = new QCheckBox(QStringLiteral("双面")); duplex->setChecked(object.value("duplex").toBool()); fields->addWidget(duplex);
        auto* monoMs = new QSpinBox; monoMs->setRange(10, 5000); monoMs->setValue(object.value("mono_page_ms").toInt());
        auto* colorMs = new QSpinBox; colorMs->setRange(10, 5000); colorMs->setValue(object.value("color_page_ms").toInt());
        fields->addWidget(label(QStringLiteral("黑白 / 彩色毫秒每面（A3 加倍）"))); fields->addWidget(monoMs); fields->addWidget(colorMs);
        auto* save = new QPushButton(QStringLiteral("保存设备")); fields->addWidget(save); connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted) return;
        object.insert("name", name->text()); object.insert("a3", a3->isChecked()); object.insert("color", color->isChecked()); object.insert("duplex", duplex->isChecked());
        object.insert("mono_page_ms", monoMs->value()); object.insert("color_page_ms", colorMs->value());
        emit shopRequested("set_printer", {{"index", i}, {"printer", object}});
    });
    auto* distribution = card(); auto* dist = new QVBoxLayout(distribution); dist->setContentsMargins(25, 20, 25, 14);
    dist->addWidget(label(QStringLiteral("本次任务分布"), "section")); chart_ = new StatisticsChart; dist->addWidget(chart_); deviceLayout->addWidget(distribution);
    auto* note = label(QStringLiteral("本地模拟打印。暂停设备接单后，当前订单会继续完成。\n队列由独立后台保存；关闭窗口后任务继续执行，重开可查看最新结果。"), "muted");
    note->setWordWrap(true); deviceLayout->addWidget(note); deviceLayout->addStretch(); stack->addWidget(devices);
    auto* shopPage = new QWidget; auto* shopLayout = new QVBoxLayout(shopPage); shopLayout->setSpacing(14);
    shopLayout->setSizeConstraint(QLayout::SetMinimumSize);
    shopLayout->addWidget(label(QStringLiteral("经营概览"), "section"));
    auto* accountingNote = label(QStringLiteral("模拟经营 · 完成后结算 · 金额单位：元。报价与期限在下单时锁定。"), "muted"); accountingNote->setWordWrap(true); shopLayout->addWidget(accountingNote);
    shopSummary_ = label(QStringLiteral("等待后台经营数据")); shopSummary_->setObjectName("shopSummary"); shopSummary_->setWordWrap(true);
    shopSummary_->setStyleSheet("font-size: 20px;"); shopSummary_->setMinimumHeight(116); shopLayout->addWidget(shopSummary_);
    stockSummary_ = label({}); stockSummary_->setObjectName("stockSummary"); stockSummary_->setWordWrap(true); shopLayout->addWidget(stockSummary_);
    auto* stockActions = new QHBoxLayout;
    for (const auto& paper : QStringList{"a4", "a3"}) {
        auto* restock = new QPushButton(QStringLiteral("补充 %1 纸张").arg(paper.toUpper())); restock->setObjectName("restock_" + paper); stockActions->addWidget(restock);
        connect(restock, &QPushButton::clicked, this, [this, paper] {
            bool ok = false; const int n = QInputDialog::getInt(this, QStringLiteral("补充库存"), QStringLiteral("增加张数"), 500, 1, 1000000, 1, &ok);
            if (ok) emit shopRequested("restock", {{"paper", paper}, {"quantity", n}});
        });
    }
    stockActions->addStretch(); shopLayout->addLayout(stockActions);
    auto* tierNote = label(QStringLiteral("服务档位\n快速：30 分钟内，原价；普通：2 小时内，95 折；灵活：8 小时内，85 折。\nA4 黑白 0.20 元/面，彩色 1.50 元/面；A3 加倍。所有价格和成本均为演示参数。\n交付时间为承诺期限，超时会单独统计；设备暂停或缺纸可能影响履约。"), "muted");
    tierNote->setWordWrap(true); shopLayout->addWidget(tierNote);
    auto* refund = new QPushButton(QStringLiteral("为记录页选中的已完成订单全额退款（模拟）")); refund->setObjectName("refundOrder"); shopLayout->addWidget(refund);
    connect(refund, &QPushButton::clicked, this, [this] {
        const auto index = table_->currentIndex();
        if (!index.isValid() || index.data(Qt::UserRole + 1).toInt() != int(JobState::Completed)) {
            feedback_->setText(QStringLiteral("请先在记录页选中已完成的订单。")); return;
        }
        emit shopRequested("refund", {{"job_id", index.data(Qt::UserRole).toString()}, {"cumulative_cents", index.data(Qt::UserRole + 5).toDouble()}});
    });
    auto* experimentActions = new QHBoxLayout;
    experimentActions->addWidget(label(QStringLiteral("模拟客户数")));
    auto* experimentCount = new QSpinBox; experimentCount->setRange(1, 2000); experimentCount->setValue(600); experimentCount->setObjectName("experimentCustomers");
    experimentActions->addWidget(experimentCount);
    auto* runExperiment = new QPushButton(QStringLiteral("运行策略对比")); runExperiment->setObjectName("runShopExperiments"); experimentActions->addWidget(runExperiment); experimentActions->addStretch();
    shopLayout->addLayout(experimentActions);
    auto* experimentTable = new QTableWidget(0, 8); experimentTable->setObjectName("experimentTable");
    experimentTable->setHorizontalHeaderLabels({QStringLiteral("实验场景"), QStringLiteral("策略"), QStringLiteral("优惠"), QStringLiteral("接单/流失"), QStringLiteral("营业额"), QStringLiteral("贡献利润"), QStringLiteral("完成/超时"), QStringLiteral("期末未完成")});
    experimentTable->setEditTriggers(QAbstractItemView::NoEditTriggers); experimentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    experimentTable->setMinimumHeight(240); shopLayout->addWidget(experimentTable, 1);
    auto* experimentNote = label(QStringLiteral("固定随机种子与 8 小时营业窗口。预算敏感客户占比 0%、30%、60%；完整报表含假设、客户输入与派发记录。"), "muted");
    experimentNote->setWordWrap(true); shopLayout->addWidget(experimentNote);
    auto* shopScroll = new QScrollArea; shopScroll->setObjectName("shopScroll"); shopScroll->setWidgetResizable(true);
    shopScroll->setWidget(shopPage); stack->addWidget(shopScroll);
    connect(runExperiment, &QPushButton::clicked, this, [this, options, experimentCount] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("保存策略实验"), options.stateDirectory + "/shop-experiments.json", "JSON (*.json)");
        if (!path.isEmpty()) emit experimentRequested(path, experimentCount->value());
    });
    if (options.shopMode) stack->setCurrentIndex(3);
    layout->addWidget(stack, 1);
    auto* footer = new QHBoxLayout; footer->setSpacing(12);
    connection_ = label(QStringLiteral("检查执行器…"), "badge"); footer->addWidget(connection_);
    feedback_ = label(QStringLiteral("新建一份任务，或加载演示任务体验经营调度。"), "muted");
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
    auto* paper = new SelectInput; paper->setObjectName("orderPaper"); paper->addItems({"A4", "A3"});
    auto* color = new QCheckBox(QStringLiteral("彩色打印")); color->setObjectName("orderColor");
    auto* duplex = new QCheckBox(QStringLiteral("双面打印")); duplex->setObjectName("orderDuplex");
    auto* service = new SelectInput; service->setObjectName("orderService");
    service->addItems({QStringLiteral("快速 · 30 分钟 · 原价"), QStringLiteral("普通 · 2 小时 · 95 折"), QStringLiteral("灵活 · 8 小时 · 85 折")}); service->setCurrentIndex(1);
    form->addWidget(label(QStringLiteral("纸张与打印规格"), "muted")); form->addWidget(paper); form->addWidget(color); form->addWidget(duplex);
    form->addWidget(label(QStringLiteral("取件期限与优惠"), "muted")); form->addWidget(service);
    paper->setEnabled(options.shopMode); color->setEnabled(options.shopMode); duplex->setEnabled(options.shopMode); service->setEnabled(options.shopMode);
    auto* sheets = label(QStringLiteral("本次打印共 8 页"), "muted"); sheets->setWordWrap(true); form->addWidget(sheets);
    auto sheetSummary = [=] {
        if (pages->value() * copies->value() > 10000) { sheets->setText(QStringLiteral("总页数不得超过 10000")); return; }
        Job job; job.pages = pages->value(); job.copies = copies->value(); job.paper = paper->currentIndex() == 1 ? PaperSize::A3 : PaperSize::A4;
        job.color = color->isChecked(); job.duplex = duplex->isChecked(); job.service = static_cast<ServiceTier>(service->currentIndex()); cps::quote(job);
        sheets->setText(paper->isEnabled() ? QStringLiteral("%1 面 · %2 张纸 · 锁定报价 ¥%3（模拟）").arg(job.sheets()).arg(job.paperSheets()).arg(job.quotedCents / 100.0, 0, 'f', 2)
                                        : QStringLiteral("本次打印共 %1 页").arg(job.sheets()));
    };
    connect(pages, &QSpinBox::valueChanged, this, sheetSummary); connect(copies, &QSpinBox::valueChanged, this, sheetSummary);
    connect(paper, &QComboBox::currentIndexChanged, this, sheetSummary); connect(service, &QComboBox::currentIndexChanged, this, sheetSummary);
    connect(color, &QCheckBox::toggled, this, sheetSummary); connect(duplex, &QCheckBox::toggled, this, sheetSummary); sheetSummary();
    auto* submit = new QPushButton(QStringLiteral("加入打印队列")); submit->setObjectName("submitJob"); submit->setProperty("role", "primary"); submit->setDefault(true); form->addWidget(submit);
    auto* import = new QPushButton(QStringLiteral("从 JSON 文件批量导入")); import->setObjectName("importJobs"); form->addWidget(import);
    connect(newTask, &QPushButton::clicked, this, [=] { sheetSummary(); submission->show(); submission->raise(); title->setFocus(); title->selectAll(); });
    qRegisterMetaType<ViewState>();
    controller_ = new PrintController(options); controller_->moveToThread(&backendThread_);
    connect(&backendThread_, &QThread::started, controller_, &PrintController::initialize);
    connect(&backendThread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(this, &PrintWindow::experimentRequested, controller_, &PrintController::runExperiments, Qt::QueuedConnection);
    connect(controller_, &PrintController::experimentReady, this, [experimentTable](const QJsonObject& report) {
        const auto runs = report.value("runs").toArray(); experimentTable->setRowCount(runs.size());
        for (int i = 0; i < runs.size(); ++i) {
            const auto r = runs[i].toObject();
            const auto policy = r.value("policy").toString();
            const QStringList cells{r.value("mode") == "fixed_orders" ? QStringLiteral("固定订单") : QStringLiteral("预算敏感 %1%").arg(r.value("budget_sensitive_percent").toInt()),
                policy == "revenue" ? QStringLiteral("收入导向") : policy == "deadline" ? QStringLiteral("期限优先") : QStringLiteral("先来先服务"),
                r.value("discounts").toBool() ? QStringLiteral("开启") : QStringLiteral("关闭"),
                QStringLiteral("%1 / %2").arg(r.value("accepted").toInt()).arg(r.value("lost_before_order").toInt()),
                QString::number(r.value("revenue_cents").toDouble()/100, 'f', 2), QString::number(r.value("contribution_cents").toDouble()/100, 'f', 2),
                QStringLiteral("%1 / %2").arg(r.value("completed").toInt()).arg(r.value("late").toInt()), QString::number(r.value("unfinished_at_close").toInt())};
            for (int c = 0; c < cells.size(); ++c) experimentTable->setItem(i, c, new QTableWidgetItem(cells[c]));
        }
    }, Qt::QueuedConnection);
    connect(this, &PrintWindow::orderRequested, controller_, &PrintController::submitOrder, Qt::QueuedConnection);
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
    connect(controller_, &PrintController::feedback, feedback_, &QLabel::setText, Qt::QueuedConnection);
    connect(controller_, &PrintController::logsReady, this, [this](const QStringList& lines) {
        log_->appendPlainText(lines.join('\n'));
    }, Qt::QueuedConnection);
    connect(submit, &QPushButton::clicked, this, [=] {
        if (title->text().trimmed().isEmpty() || pages->value() * copies->value() > 10000) {
            const auto error = QStringLiteral("请输入有效名称，总页数不得超过 10000。");
            sheets->setText(error); feedback_->setText(error); return;
        }
        if (options.shopMode) emit orderRequested({{"title", title->text()}, {"pages", pages->value()}, {"copies", copies->value()}, {"priority", priority->value()},
            {"paper", paper->currentIndex() == 1 ? "a3" : "a4"}, {"color", color->isChecked()}, {"duplex", duplex->isChecked()},
            {"service", QStringList{"express", "standard", "flexible"}[service->currentIndex()]}});
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
    emptyState_->setVisible(filter_->rowCount() == 0);
    queueSummary_->setText(QStringLiteral("%1 个任务").arg(view.jobs.size()));
    const bool available = view.connected;
    if (available) emit backendReady();
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
    shopSnapshot_ = view.shop;
    for (const auto* field : {"orderPaper", "orderColor", "orderDuplex", "orderService"})
        findChild<QWidget*>(field)->setEnabled(available && view.shop.value("shop_enabled").toBool());
    const auto fleet = view.shop.value("fleet").toArray();
    const int count = int(view.printers.size());
    fleetTable_->setRowCount(count);
    for (int i = 0; i < count; ++i) {
        const auto object = i < fleet.size() ? fleet[i].toObject() : QJsonObject{};
        const auto& printer = view.printers[i];
        const QString capability = object.isEmpty() ? QStringLiteral("A4 黑白") :
            QString(object.value("a3").toBool() ? "A3/A4" : "A4") + (object.value("color").toBool() ? QStringLiteral(" 彩色/黑白") : QStringLiteral(" 黑白")) +
            (object.value("duplex").toBool() ? QStringLiteral(" 双面") : QStringLiteral(" 单面"));
        const QStringList values{object.value("name").toString(QStringLiteral("打印机 %1").arg(i + 1)), capability,
            QStringLiteral("%1 / %2").arg(object.value("mono_page_ms").toDouble() / 1000, 0, 'f', 2).arg(object.value("color_page_ms").toDouble() / 1000, 0, 'f', 2),
            object.value("online").toBool(true) ? QStringLiteral("接单中") : QStringLiteral("暂停接单"),
            printer.jobId.isEmpty() ? QStringLiteral("空闲") : printer.title, QStringLiteral("%1 / %2 面").arg(printer.printed).arg(printer.total)};
        for (int c = 0; c < values.size(); ++c) {
            if (!fleetTable_->item(i, c)) fleetTable_->setItem(i, c, new QTableWidgetItem);
            fleetTable_->item(i, c)->setText(values[c]); fleetTable_->item(i, c)->setToolTip(values[c]);
        }
    }
    const auto business = view.shop.value("shop_metrics").toObject();
    shopSummary_->setText(view.shop.value("shop_enabled").toBool() ?
        QStringLiteral("实收营业额  ¥%1\n直接成本  ¥%2      贡献利润  ¥%3\n累计退款  ¥%4      待完成订单金额  ¥%5\n完成 %6 单 · 按时 %7 单 · 超时完成 %8 单 · 当前逾期 %9 单")
            .arg(business.value("revenue_cents").toDouble()/100, 0, 'f', 2).arg(business.value("cost_cents").toDouble()/100, 0, 'f', 2)
            .arg(business.value("contribution_cents").toDouble()/100, 0, 'f', 2).arg(business.value("refund_cents").toDouble()/100, 0, 'f', 2)
            .arg(business.value("pending_cents").toDouble()/100, 0, 'f', 2).arg(business.value("completed").toInt())
            .arg(business.value("on_time").toInt()).arg(business.value("late").toInt()).arg(business.value("overdue").toInt())
        : QStringLiteral("此目录是旧版队列。使用新的数据目录启动经营模式即可启用六台设备与订单计价。"));
    stockSummary_->setText(QStringLiteral("可用库存：A4 %1 张 / A3 %2 张。运行与重试订单已预留纸张。\n直接成本包括成功订单的纸张与墨粉估算，未计租金、人工或故障废页。").arg(view.shop.value("stock_a4").toInt()).arg(view.shop.value("stock_a3").toInt()));
    for (const auto* name : {"togglePrinter", "editPrinter", "restock_a4", "restock_a3", "refundOrder"})
        findChild<QPushButton*>(name)->setEnabled(available && view.shop.value("shop_enabled").toBool());
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
