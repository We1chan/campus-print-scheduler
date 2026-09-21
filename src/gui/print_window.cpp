#include "print_window.hpp"
#include "ui_theme.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
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
    p.setPen(QPen(QColor("#93A0B8"), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
        painter->fillRect(option.rect, QColor(selected ? "#EFF4FF" : hovered ? "#F8FAFD" : "#FFFFFF"));
        painter->setPen(QColor("#EFF2F7"));
        painter->drawLine(option.rect.bottomLeft(), option.rect.bottomRight());
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect area = option.rect.adjusted(10, 0, -10, 0);
        QFont font = option.font;
        font.setPixelSize(12);
        painter->setFont(font);
        if (index.column() == 0) {
            ui::icon(ui::Icon::Document, QColor("#8291AF")).paint(painter, QRect(area.left(), area.center().y() - 11, 22, 22));
            font.setPixelSize(13);
            font.setWeight(QFont::Medium);
            painter->setFont(font);
            painter->setPen(QColor("#37425A"));
            const QRect name(area.left() + 32, area.top() + 9, std::max(0, area.width() - 32), 21);
            painter->drawText(name, Qt::AlignVCenter, painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, name.width()));
            font.setPixelSize(10); font.setWeight(QFont::Normal); painter->setFont(font);
            painter->setPen(QColor("#A0A9B9"));
            painter->drawText(name.translated(0, 20), Qt::AlignVCenter, "# " + index.data(Qt::UserRole).toString().left(8));
        } else if (index.column() == 3) {
            const auto state = static_cast<JobState>(index.data(Qt::UserRole + 1).toInt());
            const bool success = state == JobState::Completed;
            const bool failed = state == JobState::Failed;
            const bool running = state == JobState::Running;
            const bool cancelling = state == JobState::Cancelling;
            const QColor ink(success ? "#27865D" : failed ? "#BB545A" : running ? "#4E6DD7" : cancelling ? "#A47B35" : "#8D96A7");
            const QColor fill(success ? "#EAF6F0" : failed ? "#FDEEEF" : running ? "#EDF1FF" : cancelling ? "#FFF5E4" : "#F0F3F7");
            const QString text = index.data().toString();
            const int width = std::min(area.width(), painter->fontMetrics().horizontalAdvance(text) + 18);
            painter->setPen(Qt::NoPen); painter->setBrush(fill);
            const QRect pill(area.left(), area.center().y() - 12, width, 24);
            painter->drawRoundedRect(pill, 6, 6); painter->setPen(ink);
            painter->drawText(pill, Qt::AlignCenter, text);
        } else if (index.column() == 4) {
            const int printed = index.data(Qt::UserRole + 3).toInt();
            const int sheets = index.data(Qt::UserRole + 4).toInt();
            painter->setPen(QColor("#8D98AB"));
            painter->drawText(QRect(area.left(), area.top() + 10, area.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
                              QStringLiteral("%1 / %2 页").arg(printed).arg(sheets));
            const QRectF track(area.left(), area.bottom() - 15, area.width(), 4);
            painter->setPen(Qt::NoPen); painter->setBrush(QColor("#EDF1F7")); painter->drawRoundedRect(track, 2, 2);
            if (sheets > 0 && printed > 0) {
                painter->setBrush(QColor("#8BA1EB"));
                painter->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * printed / sheets, 4), 2, 2);
            }
        } else {
            painter->setPen(QColor(index.column() == 5 ? "#8D98AB" : "#526079"));
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
    const std::array<QColor, 5> colors{QColor("#CED6E6"), QColor("#6481E8"), QColor("#68B599"), QColor("#DC919A"), QColor("#AEB5C3")};
    std::size_t total = 0;
    for (auto value : values) total += value;
    const QRectF bar(0, 6, width(), 12);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#EEF2F8")); p.drawRoundedRect(bar, 6, 6);
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
        p.setPen(QColor("#8B96AA"));
        p.drawText(QRect(left + 12, top, column - 12, 18), Qt::AlignVCenter,
                   labels[i] + "  " + QString::number(static_cast<qulonglong>(values[i])));
    }
}
PrintWindow::PrintWindow(WindowOptions options, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("校园共享打印调度 · 工作台"));
    setMinimumSize(1060, 720);
    const auto available = screen()->availableGeometry();
    resize(std::min(1360, available.width() - 40), std::min(900, available.height() - 60));
    QFont interfaceFont = font();
    const auto families = QFontDatabase::families();
    for (const auto& family : {QStringLiteral("Microsoft YaHei UI"), QStringLiteral("PingFang SC"), QStringLiteral("Noto Sans CJK SC")}) {
        if (families.contains(family)) { interfaceFont.setFamily(family); break; }
    }
    interfaceFont.setPixelSize(13);
    setFont(interfaceFont);
    QPalette light = palette();
    light.setColor(QPalette::Window, QColor("#F4F6FA"));
    light.setColor(QPalette::WindowText, QColor("#263248"));
    light.setColor(QPalette::Base, Qt::white);
    light.setColor(QPalette::Text, QColor("#354159"));
    light.setColor(QPalette::Button, Qt::white);
    light.setColor(QPalette::ButtonText, QColor("#56647B"));
    light.setColor(QPalette::Highlight, QColor("#DDE6FF"));
    light.setColor(QPalette::HighlightedText, QColor("#2B438D"));
    setPalette(light);
    setStyleSheet(ui::styleSheet());

    auto* central = new QWidget(this);
    central->setObjectName("workspace");
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(16);

    auto* header = new QHBoxLayout;
    header->setSpacing(14);
    auto* mark = new QLabel;
    mark->setObjectName("brandMark"); mark->setFixedSize(44, 44);
    mark->setAlignment(Qt::AlignCenter); mark->setPixmap(ui::icon(ui::Icon::Document, Qt::white).pixmap(27, 27));
    header->addWidget(mark);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto* heading = label(QStringLiteral("打印工作台")); heading->setObjectName("pageTitle");
    titles->addWidget(heading);
    titles->addWidget(label(QStringLiteral("校园共享打印  /  双设备协同调度"), "muted"));
    header->addLayout(titles); header->addStretch();
    if (options.demoMode) header->addWidget(label(QStringLiteral("演示模式"), "badge"), 0, Qt::AlignVCenter);
    connection_ = label(QStringLiteral("检查执行器…"), "badge");
    header->addWidget(connection_, 0, Qt::AlignVCenter);
    auto* check = new QPushButton;
    check->setIcon(ui::icon(ui::Icon::Refresh)); check->setProperty("role", "quiet");
    check->setToolTip(QStringLiteral("重新检查本地执行器")); check->setAccessibleName(QStringLiteral("重新检查执行器"));
    header->addWidget(check);
    auto* report = new QPushButton(QStringLiteral("导出报表"));
    report->setIcon(ui::icon(ui::Icon::Export)); header->addWidget(report);
    auto* folder = new QPushButton;
    folder->setProperty("role", "quiet"); folder->setIcon(ui::icon(ui::Icon::Folder));
    folder->setToolTip(QStringLiteral("打开运行目录\n") + options.stateDirectory);
    folder->setAccessibleName(QStringLiteral("打开运行目录")); header->addWidget(folder);
    connect(folder, &QPushButton::clicked, this, [this, options] {
        if (!QDir().mkpath(options.stateDirectory) || !QDesktopServices::openUrl(QUrl::fromLocalFile(options.stateDirectory)))
            feedback_->setText(QStringLiteral("无法打开运行目录：") + options.stateDirectory);
    });
    layout->addLayout(header);

    auto* metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    const QStringList metricTitles{QStringLiteral("已完成任务"), QStringLiteral("等待队列"), QStringLiteral("平均首次等待"), QStringLiteral("设备利用率")};
    for (int i = 0; i < 4; ++i) {
        auto* box = card("metric");
        auto* body = new QVBoxLayout(box);
        body->setContentsMargins(18, 12, 18, 12); body->setSpacing(3);
        body->addWidget(label(metricTitles[i], "metricLabel"));
        metricValues_[i] = label(i < 2 ? "0" : i == 2 ? QStringLiteral("0.0 秒") : "0.0%", "metricValue");
        body->addWidget(metricValues_[i]);
        metricHints_[i] = label(QStringLiteral("本次会话"), "metricHint"); body->addWidget(metricHints_[i]);
        metrics->addWidget(box);
    }
    layout->addLayout(metrics);

    auto* content = new QHBoxLayout;
    content->setSpacing(18);
    auto* working = new QVBoxLayout;
    working->setSpacing(16);
    auto* printerHeading = new QHBoxLayout;
    printerHeading->addWidget(label(QStringLiteral("打印设备"), "section"));
    printerHeading->addStretch(); printerHeading->addWidget(label(QStringLiteral("2 台模拟打印机"), "muted"));
    working->addLayout(printerHeading);
    auto* printers = new QHBoxLayout;
    printers->setSpacing(12);
    for (int i = 0; i < 2; ++i) {
        auto* box = card();
        auto* body = new QVBoxLayout(box);
        body->setContentsMargins(16, 14, 16, 14); body->setSpacing(9);
        auto* top = new QHBoxLayout;
        auto* tile = label({}, "iconTile"); tile->setFixedSize(34, 34); tile->setAlignment(Qt::AlignCenter);
        tile->setPixmap(ui::icon(ui::Icon::Printer).pixmap(22, 22));
        top->addWidget(tile); top->addSpacing(3);
        top->addWidget(label(QStringLiteral("打印机 0%1").arg(i + 1), "section")); top->addStretch();
        printerStates_[i] = label(QStringLiteral("就绪"), "badge"); printerStates_[i]->setProperty("tone", "ready");
        top->addWidget(printerStates_[i]); body->addLayout(top);
        printerLabels_[i] = label(QStringLiteral("等待下一个任务"), "printerTitle");
        printerLabels_[i]->setMinimumWidth(0); printerLabels_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        body->addWidget(printerLabels_[i]);
        printerDetails_[i] = label(QStringLiteral("提交后将自动分配任务"), "muted");
        printerDetails_[i]->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); body->addWidget(printerDetails_[i]);
        progressBars_[i] = new QProgressBar;
        progressBars_[i]->setRange(0, 100); progressBars_[i]->setValue(0); progressBars_[i]->setTextVisible(false);
        body->addWidget(progressBars_[i]);
        printerProgress_[i] = label(QStringLiteral("当前没有运行任务"), "muted");
        body->addWidget(printerProgress_[i]); printers->addWidget(box);
    }
    working->addLayout(printers);

    auto* queue = card();
    auto* queueLayout = new QVBoxLayout(queue);
    queueLayout->setContentsMargins(16, 14, 16, 12); queueLayout->setSpacing(12);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(label(QStringLiteral("任务队列"), "section"));
    queueSummary_ = label(QStringLiteral("0 个任务"), "muted"); actions->addWidget(queueSummary_); actions->addStretch();
    auto* pause = new QPushButton(QStringLiteral("暂停派发"));
    pause->setObjectName("pauseDispatch"); pause->setCheckable(true); pause->setIcon(ui::icon(ui::Icon::Pause));
    pause->setToolTip(QStringLiteral("暂停领取新任务；已经运行的任务继续打印。")); actions->addWidget(pause);
    cancel_ = new QPushButton(QStringLiteral("取消任务"));
    cancel_->setObjectName("cancelJob"); cancel_->setProperty("role", "danger"); cancel_->setEnabled(false); actions->addWidget(cancel_);
    queueLayout->addLayout(actions);
    auto* filters = new QHBoxLayout;
    auto* filter = new SelectInput;
    filter->setObjectName("queueFilter");
    filter->addItems({QStringLiteral("全部任务"), QStringLiteral("等待中"), QStringLiteral("打印中"),
        QStringLiteral("已完成"), QStringLiteral("失败"), QStringLiteral("已取消")});
    filter->setMinimumWidth(112); filters->addWidget(filter); filters->addStretch();
    filters->addWidget(label(QStringLiteral("选择任务后可执行操作"), "muted"));
    queueLayout->addLayout(filters);
    model_ = new JobTableModel(this);
    filter_ = new QSortFilterProxyModel(this); filter_->setSourceModel(model_); filter_->setFilterKeyColumn(3);
    table_ = new QTableView; table_->setObjectName("jobsTable"); table_->setModel(filter_);
    table_->setItemDelegate(new QueueDelegate(table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->setShowGrid(false); table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight); table_->setMinimumHeight(150);
    table_->verticalHeader()->setVisible(false); table_->verticalHeader()->setDefaultSectionSize(58);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->setColumnWidth(1, 56); table_->setColumnWidth(2, 58); table_->setColumnWidth(3, 83);
    table_->setColumnWidth(4, 118); table_->setColumnWidth(5, 142); table_->hideColumn(6);
    queueLayout->addWidget(table_, 1);
    auto* overlay = new QVBoxLayout(table_->viewport());
    overlay->setContentsMargins(0, 0, 0, 0);
    emptyState_ = new QWidget;
    emptyState_->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* emptyLayout = new QVBoxLayout(emptyState_);
    emptyLayout->setSpacing(8); emptyLayout->addStretch();
    auto* emptyIcon = new QLabel;
    emptyIcon->setPixmap(ui::icon(ui::Icon::Document, QColor("#BAC6DD")).pixmap(36, 36));
    emptyIcon->setAlignment(Qt::AlignCenter); emptyLayout->addWidget(emptyIcon);
    auto* emptyTitle = label(QStringLiteral("这里暂时没有任务"), "section"); emptyTitle->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    auto* emptyHint = label(QStringLiteral("从右侧提交新任务，或切换状态筛选。"), "muted"); emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyHint); emptyLayout->addStretch(); overlay->addWidget(emptyState_);
    if (options.demoMode) {
        auto* demo = new QHBoxLayout;
        demo->setSpacing(8); demo->addWidget(label(QStringLiteral("恢复演示"), "eyebrow")); demo->addStretch();
        fault_ = new QPushButton(QStringLiteral("模拟进程崩溃"));
        fault_->setObjectName("injectFault"); fault_->setEnabled(false); fault_->setProperty("role", "quiet");
        fault_->setToolTip(QStringLiteral("中断选中任务的工作进程，观察自动重试。")); demo->addWidget(fault_);
        disconnect_ = new QPushButton(QStringLiteral("断开控制连接"));
        disconnect_->setObjectName("disconnectControl"); disconnect_->setEnabled(false); disconnect_->setProperty("role", "quiet");
        disconnect_->setToolTip(QStringLiteral("断开选中任务的连接，观察自动重连。")); demo->addWidget(disconnect_);
        queueLayout->addLayout(demo);
    }
    working->addWidget(queue, 1);
    content->addLayout(working, 1);

    auto* side = new QScrollArea;
    side->setWidgetResizable(true); side->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    side->setMinimumWidth(320); side->setMaximumWidth(342); side->viewport()->setAutoFillBackground(false);
    auto* sideContent = new QWidget; sideContent->setObjectName("sideContent");
    sideContent->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* sideLayout = new QVBoxLayout(sideContent);
    sideLayout->setContentsMargins(0, 0, 0, 0); sideLayout->setSpacing(14);
    auto* submission = card();
    auto* form = new QVBoxLayout(submission);
    form->setContentsMargins(18, 16, 18, 16); form->setSpacing(9);
    form->addWidget(label(QStringLiteral("新建打印任务"), "section")); form->addSpacing(4);
    form->addWidget(label(QStringLiteral("文档名称"), "muted"));
    auto* title = new QLineEdit(QStringLiteral("操作系统课设文档"));
    title->setObjectName("jobTitle"); title->setMaxLength(200); title->setPlaceholderText(QStringLiteral("给文档起一个名称"));
    form->addWidget(title); form->addSpacing(3);
    auto* numbers = new QHBoxLayout; numbers->setSpacing(8);
    auto* pages = new NumberInput; pages->setObjectName("jobPages"); pages->setRange(1, 1000); pages->setValue(8);
    auto* copies = new NumberInput; copies->setObjectName("jobCopies"); copies->setRange(1, 100);
    auto* priority = new NumberInput; priority->setObjectName("jobPriority"); priority->setRange(0, 100);
    priority->setToolTip(QStringLiteral("数值越大越优先；老化策略下等待每 30 秒增加 1 点。"));
    const std::array<QSpinBox*, 3> inputs{pages, copies, priority};
    const QStringList numberLabels{QStringLiteral("页数"), QStringLiteral("份数"), QStringLiteral("优先级")};
    for (int i = 0; i < 3; ++i) {
        auto* field = new QVBoxLayout; field->setSpacing(6); field->addWidget(label(numberLabels[i], "muted"));
        inputs[i]->setMinimumWidth(0); field->addWidget(inputs[i]); numbers->addLayout(field);
    }
    form->addLayout(numbers); form->addSpacing(3);
    form->addWidget(label(QStringLiteral("等待任务排序"), "muted"));
    auto* policy = new SelectInput;
    policy->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    policy->addItems({QStringLiteral("先来先服务  /  FCFS"), QStringLiteral("短作业优先  /  SJF"), QStringLiteral("优先级 + 等待老化")});
    policy->setObjectName("schedulingPolicy"); form->addWidget(policy);
    auto* sheets = label(QStringLiteral("本次打印共 8 页"), "muted"); form->addWidget(sheets); form->addSpacing(4);
    auto sheetSummary = [pages, copies, sheets] {
        const int count = pages->value() * copies->value();
        sheets->setText(count > 10000 ? QStringLiteral("总页数超过 10000，请减少页数或份数") : QStringLiteral("本次打印共 %1 页").arg(count));
    };
    connect(pages, &QSpinBox::valueChanged, this, sheetSummary);
    connect(copies, &QSpinBox::valueChanged, this, sheetSummary);
    auto* submit = new QPushButton(QStringLiteral("加入打印队列"));
    submit->setObjectName("submitJob"); submit->setProperty("role", "primary"); submit->setIcon(ui::icon(ui::Icon::Plus, Qt::white));
    form->addWidget(submit);
    auto* import = new QPushButton(QStringLiteral("从文件批量导入"));
    import->setIcon(ui::icon(ui::Icon::Import)); form->addWidget(import);
    sideLayout->addWidget(submission);
    auto* distribution = card();
    auto* distributionLayout = new QVBoxLayout(distribution);
    distributionLayout->setContentsMargins(18, 14, 18, 8); distributionLayout->setSpacing(4);
    distributionLayout->addWidget(label(QStringLiteral("本次任务分布"), "section"));
    chart_ = new StatisticsChart; distributionLayout->addWidget(chart_);
    sideLayout->addWidget(distribution);
    auto* note = label(QStringLiteral("队列保存在本次会话中。\n退出前请完成或取消剩余任务。"), "muted");
    note->setWordWrap(true); sideLayout->addWidget(note); sideLayout->addStretch();
    side->setWidget(sideContent); content->addWidget(side);
    layout->addLayout(content, 1);

    auto* footer = new QHBoxLayout;
    footer->setSpacing(12);
    feedback_ = label(QStringLiteral("准备就绪，从右侧添加第一份打印任务。"), "muted");
    feedback_->setObjectName("feedback"); feedback_->setWordWrap(true);
    footer->addWidget(feedback_, 1);
    logToggle_ = new QToolButton;
    logToggle_->setProperty("role", "quiet"); logToggle_->setCheckable(true);
    logToggle_->setIcon(ui::icon(ui::Icon::Activity)); logToggle_->setText(QStringLiteral("运行日志"));
    logToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); footer->addWidget(logToggle_);
    layout->addLayout(footer);
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true); log_->setMaximumBlockCount(500); log_->setFixedHeight(128);
    log_->setPlaceholderText(QStringLiteral("执行事件与错误日志")); log_->hide();
    layout->addWidget(log_);
    connect(logToggle_, &QToolButton::toggled, log_, &QWidget::setVisible);
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
        pause->setText(paused ? QStringLiteral("继续派发") : QStringLiteral("暂停派发"));
        pause->setIcon(ui::icon(paused ? ui::Icon::Play : ui::Icon::Pause));
        emit pauseRequested(paused);
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { emit cancelRequested(table_->currentIndex().data(Qt::UserRole).toString()); });
    connect(check, &QPushButton::clicked, this, &PrintWindow::checkRequested);
    connect(report, &QPushButton::clicked, this, [this, options] {
        const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出会话统计"), options.stateDirectory + "/report.json", "JSON (*.json)");
        if (!path.isEmpty()) emit exportRequested(path);
    });
    connect(filter, &QComboBox::currentIndexChanged, this, [this, filter](int index) {
        filter_->setFilterFixedString(index == 0 ? QString() : filter->currentText());
        emptyState_->setVisible(filter_->rowCount() == 0);
        updateSelection();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PrintWindow::updateSelection);
    backendThread_.start();
}
PrintWindow::~PrintWindow() { backendThread_.quit(); backendThread_.wait(); }
void PrintWindow::importFile(const QString& path) { emit importRequested(path); }
void PrintWindow::applyView(const ViewState& view) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    model_->update(view.jobs);
    emptyState_->setVisible(filter_->rowCount() == 0);
    queueSummary_->setText(QStringLiteral("%1 个任务").arg(view.jobs.size()));
    const bool available = !view.connection.contains(QStringLiteral("不可用"));
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
