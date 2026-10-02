#include "student_window.hpp"
#include "ui_theme.hpp"
#include "view_text.hpp"
#include "cps/core/shop.hpp"
#include "cps/runtime/shop_io.hpp"
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>

namespace cps::gui {
namespace {
using ui::label;
using ui::card;
using ui::tone;
constexpr qint64 MaxDocumentBytes = 64 * 1024 * 1024;

QString sizeText(qint64 bytes) {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
}
// Long names keep both ends: the start identifies the document, the end its type.
QString shortName(const QString& name) {
    return name.size() <= 44 ? name : name.left(26) + QStringLiteral("…") + name.right(14);
}
bool startsAt(const QByteArray& data, qsizetype at, const char* word) {
    const auto length = static_cast<qsizetype>(std::strlen(word));
    return at >= 0 && at + length <= data.size() && std::memcmp(data.constData() + at, word, static_cast<std::size_t>(length)) == 0;
}
qsizetype skipSpace(const QByteArray& data, qsizetype at) {
    while (at < data.size() && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n')) ++at;
    return at;
}
// Best effort without a PDF library: count page objects, or fall back to the page
// tree's /Count when the objects sit inside compressed streams.
int pdfPages(const QByteArray& data) {
    int pages = 0;
    for (auto at = data.indexOf("/Type"); at >= 0; at = data.indexOf("/Type", at + 5)) {
        const auto name = skipSpace(data, at + 5);
        if (!startsAt(data, name, "/Page")) continue;
        const char next = name + 5 < data.size() ? data[name + 5] : ' ';
        if (!((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z'))) ++pages; // not /Pages
    }
    if (pages > 0) return pages;
    for (auto at = data.indexOf("/Count"); at >= 0; at = data.indexOf("/Count", at + 6)) {
        auto digit = skipSpace(data, at + 6);
        int count = 0;
        while (digit < data.size() && data[digit] >= '0' && data[digit] <= '9' && count < 100000) count = count * 10 + (data[digit++] - '0');
        pages = std::max(pages, count);
    }
    return pages;
}
}

DocumentInfo inspectDocument(const QString& path) {
    const QFileInfo info(path);
    DocumentInfo document;
    document.name = info.fileName().left(200);
    document.bytes = info.size();
    QFile file(path);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) { document.error = QStringLiteral("文件无法读取"); return document; }
    if (document.bytes == 0) { document.error = QStringLiteral("文件是空的"); return document; }
    if (document.bytes > MaxDocumentBytes) { document.error = QStringLiteral("文件超过 64 MB"); return document; }
    const auto data = file.readAll();
    const auto suffix = info.suffix().toLower();
    if (data.startsWith("%PDF")) document.pages = pdfPages(data);
    else if (QStringList{"txt", "md", "csv", "log", "c", "h", "cpp", "hpp", "py", "java", "json", "tex"}.contains(suffix))
        document.pages = static_cast<int>((data.count('\n') + 50) / 50); // about 50 lines per page
    else if (QStringList{"png", "jpg", "jpeg", "bmp", "gif"}.contains(suffix)) document.pages = 1;
    document.pages = std::clamp(document.pages, 0, 1000);
    return document;
}

StudentWindow::StudentWindow(WindowOptions options, const QString& studentId, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("校园打印 · 学生自助下单"));
    setMinimumSize(920, 640);
    const auto available = screen()->availableGeometry();
    resize(std::min(1180, available.width() - 40), std::min(820, available.height() - 60));
    setAcceptDrops(true);
    ui::applyTheme(*this);
    auto* central = new QWidget(this); central->setObjectName("workspace");
    auto* layout = new QVBoxLayout(central); layout->setContentsMargins(24, 10, 24, 12); layout->setSpacing(12);
    auto* header = new QHBoxLayout; header->setSpacing(10);
    header->addWidget(ui::brand(ui::Icon::Printer, QStringLiteral("校园打印"), QStringLiteral("学生自助下单"))); header->addSpacing(20);
    auto* navigation = new QButtonGroup(this); navigation->setExclusive(true);
    pages_ = new QStackedWidget; pages_->setObjectName("studentPages");
    const QStringList names{QStringLiteral("下单打印"), QStringLiteral("我的订单"), QStringLiteral("排队情况")};
    const QStringList navIds{"navOrder", "navMine", "navQueue"};
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(names[i]); button->setObjectName(navIds[i]); button->setProperty("role", "nav");
        button->setCheckable(true); button->setChecked(i == 0); button->setToolTip(QStringLiteral("Ctrl+%1").arg(i + 1));
        navigation->addButton(button, i); header->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
        connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this), &QShortcut::activated, this, [this, i] { pages_->setCurrentIndex(i); });
    }
    connect(pages_, &QStackedWidget::currentChanged, this, [navigation](int index) { navigation->button(index)->setChecked(true); });
    header->addStretch();
    header->addWidget(label(QStringLiteral("学号"), "muted"));
    student_ = new QLineEdit(studentId.trimmed().isEmpty() ? QStringLiteral("2024000001") : studentId.trimmed().left(32));
    student_->setObjectName("studentId"); student_->setMaxLength(32); student_->setFixedWidth(150);
    student_->setPlaceholderText(QStringLiteral("填写学号"));
    student_->setToolTip(QStringLiteral("订单按学号归属；换一个学号只能看到那个学号的订单。"));
    header->addWidget(student_); layout->addLayout(header);

    auto* metrics = new QHBoxLayout;
    const QStringList metricTitles{QStringLiteral("当前排队"), QStringLiteral("预计清空队列"), QStringLiteral("我的进行中订单"), QStringLiteral("可取件")};
    for (int i = 0; i < 4; ++i) {
        auto* box = card("metric"); auto* m = new QVBoxLayout(box); m->setContentsMargins(18, 10, 18, 10);
        m->addWidget(label(metricTitles[i], "metricLabel"));
        metricValues_[i] = label("—", "metricValue"); metricValues_[i]->setObjectName(QStringLiteral("studentMetric%1").arg(i)); m->addWidget(metricValues_[i]);
        metricHints_[i] = label({}, "metricHint"); m->addWidget(metricHints_[i]); metrics->addWidget(box);
    }
    layout->addLayout(metrics);

    // Page 1: upload, options, price with estimated wait, simulated payment.
    auto* orderArea = new QScrollArea; orderArea->setObjectName("orderScroll");
    orderArea->setWidgetResizable(true); orderArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* orderPage = new QWidget; orderPage->setObjectName("queueContent");
    auto* columns = new QHBoxLayout(orderPage); columns->setContentsMargins(0, 0, 0, 0); columns->setSpacing(14);
    auto* documentCard = card(); auto* form = new QVBoxLayout(documentCard); form->setContentsMargins(22, 18, 22, 18); form->setSpacing(10);
    form->addWidget(label(QStringLiteral("① 上传文件"), "step"));
    drop_ = card("drop"); drop_->setObjectName("dropZone"); drop_->setMinimumHeight(86);
    auto* dropLayout = new QHBoxLayout(drop_); dropLayout->setContentsMargins(16, 14, 16, 14); dropLayout->setSpacing(12);
    auto* sheet = new QLabel; sheet->setPixmap(ui::icon(ui::Icon::Document, QColor("#6F8FD8")).pixmap(30, 30)); dropLayout->addWidget(sheet);
    auto* fileLines = new QVBoxLayout; fileLines->setSpacing(3); fileLines->addStretch();
    // Ignored width: a long file name is shortened instead of widening the window.
    fileName_ = label({}, "fileName"); fileName_->setObjectName("fileName"); fileName_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    fileLines->addWidget(fileName_);
    file_ = label({}, "muted"); file_->setObjectName("fileInfo"); file_->setWordWrap(true); fileLines->addWidget(file_);
    fileLines->addStretch(); dropLayout->addLayout(fileLines, 1);
    remove_ = new QPushButton(QStringLiteral("移除")); remove_->setObjectName("removeFile"); remove_->setProperty("role", "quiet"); dropLayout->addWidget(remove_);
    choose_ = new QPushButton(QStringLiteral("选择文件")); choose_->setObjectName("chooseFile"); choose_->setIcon(ui::icon(ui::Icon::Import));
    choose_->setToolTip(QStringLiteral("Ctrl+O")); dropLayout->addWidget(choose_); form->addWidget(drop_);
    form->addWidget(label(QStringLiteral("文档名称"), "muted"));
    title_ = new QLineEdit; title_->setObjectName("orderTitle"); title_->setMaxLength(200);
    title_->setPlaceholderText(QStringLiteral("选择文件后自动填写")); form->addWidget(title_);
    pageCount_ = new ui::NumberInput; pageCount_->setObjectName("orderPages"); pageCount_->setRange(1, 1000); pageCount_->setValue(8);
    copies_ = new ui::NumberInput; copies_->setObjectName("orderCopies"); copies_->setRange(1, 100);
    auto* numbers = new QHBoxLayout;
    for (auto [caption, input] : {std::pair<QString, QSpinBox*>{QStringLiteral("页数"), pageCount_}, {QStringLiteral("份数"), copies_}}) {
        auto* field = new QVBoxLayout; field->addWidget(label(caption, "muted")); field->addWidget(input); numbers->addLayout(field);
    }
    form->addLayout(numbers);
    form->addSpacing(6); form->addWidget(label(QStringLiteral("② 打印设置"), "step"));
    auto* specification = new QHBoxLayout; specification->setSpacing(16);
    paper_ = new ui::SelectInput; paper_->setObjectName("orderPaper"); paper_->addItems({"A4", "A3"}); specification->addWidget(paper_, 1);
    color_ = new ui::CheckBox(QStringLiteral("彩色打印")); color_->setObjectName("orderColor"); specification->addWidget(color_);
    duplex_ = new ui::CheckBox(QStringLiteral("双面打印")); duplex_->setObjectName("orderDuplex"); specification->addWidget(duplex_);
    form->addLayout(specification);
    form->addWidget(label(QStringLiteral("取件期限与优惠：愿意多等一会儿，价格更低"), "muted"));
    service_ = new QButtonGroup(this); service_->setExclusive(true);
    auto* tiers = new QHBoxLayout;
    const QStringList tierIds{"serviceExpress", "serviceStandard", "serviceFlexible"};
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton; button->setObjectName(tierIds[i]); button->setProperty("role", "choice");
        button->setCheckable(true); button->setChecked(i == 1); service_->addButton(button, i); tiers->addWidget(button);
    }
    form->addLayout(tiers); form->addStretch(); columns->addWidget(documentCard, 3);

    auto* payCard = card(); auto* pay = new QVBoxLayout(payCard); pay->setContentsMargins(22, 18, 22, 18); pay->setSpacing(10);
    pay->addWidget(label(QStringLiteral("③ 费用与预计时间"), "step"));
    price_ = label(QStringLiteral("¥0.00"), "price"); price_->setObjectName("quotePrice"); pay->addWidget(price_);
    priceDetail_ = label({}, "muted"); priceDetail_->setObjectName("quoteDetail"); priceDetail_->setWordWrap(true); pay->addWidget(priceDetail_);
    auto* forecast = card("summary"); auto* forecastLayout = new QVBoxLayout(forecast); forecastLayout->setContentsMargins(14, 10, 14, 10); forecastLayout->setSpacing(4);
    forecastLayout->addWidget(label(QStringLiteral("现在下单"), "muted"));
    eta_ = label(QStringLiteral("正在连接店铺…"), "eta"); eta_->setObjectName("quoteEta"); eta_->setWordWrap(true); forecastLayout->addWidget(eta_);
    deadline_ = label({}, "muted"); deadline_->setObjectName("quoteDeadline"); deadline_->setWordWrap(true); forecastLayout->addWidget(deadline_);
    pay->addWidget(forecast);
    pay->addSpacing(6); pay->addWidget(label(QStringLiteral("④ 支付方式（模拟）"), "step"));
    payment_ = new QButtonGroup(this); payment_->setExclusive(true);
    auto* methods = new QHBoxLayout;
    for (const auto method : {PaymentMethod::Wechat, PaymentMethod::Alipay, PaymentMethod::CampusCard}) {
        auto* button = new QPushButton(text::payment(method)); button->setObjectName("pay_" + paymentName(method));
        button->setProperty("role", "choice"); button->setCheckable(true); button->setChecked(method == PaymentMethod::Wechat);
        payment_->addButton(button, static_cast<int>(method)); methods->addWidget(button);
    }
    pay->addLayout(methods);
    pay_ = new QPushButton(QStringLiteral("支付并提交订单")); pay_->setObjectName("payOrder"); pay_->setProperty("role", "primary"); pay_->setEnabled(false);
    pay->addWidget(pay_);
    // Says what is still missing, so a disabled button never leaves the student guessing.
    payHint_ = label({}, "muted"); payHint_->setObjectName("payHint"); payHint_->setWordWrap(true); tone(payHint_, "waiting"); pay->addWidget(payHint_);
    auto* payNote = label(QStringLiteral("不会产生真实扣款。订单取消或打印失败时按原路退款（同样为模拟）。"), "muted");
    payNote->setWordWrap(true); pay->addWidget(payNote); pay->addStretch(); columns->addWidget(payCard, 2);
    orderArea->setWidget(orderPage); pages_->addWidget(orderArea);

    // Page 2: only this student's orders.
    auto* mine = card(); auto* mineLayout = new QVBoxLayout(mine); mineLayout->setContentsMargins(18, 16, 18, 14); mineLayout->setSpacing(10);
    auto* mineHead = new QHBoxLayout; mineHead->setSpacing(8); mineHead->addWidget(label(QStringLiteral("我的订单"), "section"));
    ordersSummary_ = label({}, "muted"); mineHead->addWidget(ordersSummary_); mineHead->addStretch();
    cancel_ = new QPushButton(QStringLiteral("取消订单并退款")); cancel_->setObjectName("cancelOrder"); cancel_->setProperty("role", "danger");
    cancel_->setEnabled(false); mineHead->addWidget(cancel_); mineLayout->addLayout(mineHead);
    orders_ = ui::listTable("myOrders", {QStringLiteral("文档"), QStringLiteral("规格"), QStringLiteral("金额 / 支付"), QStringLiteral("状态"),
        QStringLiteral("排队与预计时间"), QStringLiteral("进度"), QStringLiteral("取件码")});
    // Only the document name gives way; prices, states and estimates are never elided.
    orders_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    orders_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mineLayout->addWidget(orders_, 1);
    ordersEmpty_ = ui::emptyState(orders_, QStringLiteral("还没有订单"), QStringLiteral("在“下单打印”上传文件并支付后，订单会出现在这里"));
    auto* mineNote = label(QStringLiteral("预计时间按当前队列、调度策略和设备推算，不含之后到达的订单，仅供参考；承诺期限以订单的取件期限为准。"), "muted");
    mineNote->setWordWrap(true); mineLayout->addWidget(mineNote); pages_->addWidget(mine);

    // Page 3: the shop as a student may see it, without other students' document names.
    auto* shopArea = new QScrollArea; shopArea->setObjectName("queueScroll");
    shopArea->setWidgetResizable(true); shopArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* shop = new QWidget; shop->setObjectName("queueContent");
    auto* shopLayout = new QVBoxLayout(shop); shopLayout->setContentsMargins(0, 0, 0, 0); shopLayout->setSpacing(14);
    auto* deviceCard = card(); auto* deviceLayout = new QVBoxLayout(deviceCard); deviceLayout->setContentsMargins(18, 16, 18, 14); deviceLayout->setSpacing(10);
    deviceLayout->addWidget(label(QStringLiteral("设备状态"), "section"));
    devices_ = ui::listTable("shopDevices", {QStringLiteral("设备"), QStringLiteral("能力"), QStringLiteral("状态"), QStringLiteral("预计空闲")});
    devices_->setSelectionMode(QAbstractItemView::NoSelection);
    deviceLayout->addWidget(devices_); shopLayout->addWidget(deviceCard);
    auto* queueCard = card(); auto* queueLayout = new QVBoxLayout(queueCard); queueLayout->setContentsMargins(18, 16, 18, 14); queueLayout->setSpacing(10);
    queueLayout->addWidget(label(QStringLiteral("等待队列"), "section"));
    queue_ = ui::listTable("shopQueue", {QStringLiteral("顺位"), QStringLiteral("订单"), QStringLiteral("规格"), QStringLiteral("预计开始"), QStringLiteral("预计完成")});
    queue_->setSelectionMode(QAbstractItemView::NoSelection); queue_->setMinimumHeight(180); queueLayout->addWidget(queue_, 1);
    queueEmpty_ = ui::emptyState(queue_, QStringLiteral("现在不用排队"), QStringLiteral("有订单等待时，这里按预计开始的先后列出"));
    auto* queueNote = label(QStringLiteral("为保护隐私，不显示其他同学的文档名称。顺位是按当前策略推算的开始顺序，新订单或设备变化都可能改变它。"), "muted");
    queueNote->setWordWrap(true); queueLayout->addWidget(queueNote); shopLayout->addWidget(queueCard, 1);
    shopArea->setWidget(shop); pages_->addWidget(shopArea);
    layout->addWidget(pages_, 1);

    auto* footer = new QHBoxLayout; footer->setSpacing(12);
    connection_ = label(QStringLiteral("正在连接店铺…"), "badge"); connection_->setObjectName("connection"); footer->addWidget(connection_);
    feedback_ = label(QStringLiteral("选择要打印的文件，确认规格和价格后支付即可进入队列。"), "muted");
    feedback_->setObjectName("feedback"); feedback_->setWordWrap(true); footer->addWidget(feedback_, 1);
    layout->addLayout(footer); setCentralWidget(central);

    quoteDelay_ = new QTimer(this); quoteDelay_->setSingleShot(true); quoteDelay_->setInterval(150);
    connect(quoteDelay_, &QTimer::timeout, this, [this] { if (view_.connected) emit quoteRequested(order(), ++quoteToken_); });
    // The queue moves even when the form does not, so keep the shown estimate fresh.
    auto* quoteRefresh = new QTimer(this);
    connect(quoteRefresh, &QTimer::timeout, this, [this] { if (pages_->currentIndex() == 0 && !paymentDialog_) quoteDelay_->start(); });
    quoteRefresh->start(2000);
    connect(pageCount_, &QSpinBox::valueChanged, this, &StudentWindow::updatePrice);
    connect(copies_, &QSpinBox::valueChanged, this, &StudentWindow::updatePrice);
    connect(paper_, &QComboBox::currentIndexChanged, this, &StudentWindow::updatePrice);
    connect(color_, &QCheckBox::toggled, this, &StudentWindow::updatePrice);
    connect(duplex_, &QCheckBox::toggled, this, &StudentWindow::updatePrice);
    connect(service_, &QButtonGroup::idClicked, this, &StudentWindow::updatePrice);
    connect(title_, &QLineEdit::textChanged, this, &StudentWindow::updateActions);
    connect(student_, &QLineEdit::textChanged, this, [this] { applyView(view_); });
    connect(choose_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要打印的文件"), {},
            QStringLiteral("文档 (*.pdf *.txt *.md *.doc *.docx *.ppt *.pptx *.png *.jpg *.jpeg);;所有文件 (*)"));
        if (!path.isEmpty()) setDocument(path);
    });
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, [this] { pages_->setCurrentIndex(0); choose_->click(); });
    connect(remove_, &QPushButton::clicked, this, [this] {
        clearDocument(); ui::say(feedback_, QStringLiteral("已移除文件。重新选择后即可下单。"));
    });
    connect(pay_, &QPushButton::clicked, this, &StudentWindow::showPayment);
    connect(orders_, &QTableWidget::itemSelectionChanged, this, &StudentWindow::updateActions);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        const int row = orders_->currentRow();
        if (row < 0 || row >= orderIds_.size()) return;
        const auto id = orderIds_[row];
        const auto job = std::find_if(view_.jobs.cbegin(), view_.jobs.cend(), [&id](const JobView& item) { return item.id == id; });
        if (job == view_.jobs.cend()) return;
        // Cancelling stops a print that may already be on paper, so ask once.
        ui::confirm(this, "cancelDialog", QStringLiteral("取消这个订单？"),
            shortName(job->title) + (job->state == JobState::Running ? QStringLiteral("\n订单正在打印，取消后会停止，已打印的部分作废。")
                : QStringLiteral("\n订单还在排队，取消后立即退出队列。")) +
            QStringLiteral("\n%1 将按原路退回（模拟）。").arg(text::money(job->quotedCents)),
            QStringLiteral("取消订单并退款"), [this, id] {
                successMessage_ = QStringLiteral("已提交取消；停止打印后按原路退款（模拟）。");
                emit cancelRequested(id);
            });
    });

    qRegisterMetaType<ViewState>();
    controller_ = new PrintController(options); controller_->moveToThread(&backendThread_);
    connect(&backendThread_, &QThread::started, controller_, &PrintController::initialize);
    connect(&backendThread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(this, &StudentWindow::quoteRequested, controller_, &PrintController::requestQuote, Qt::QueuedConnection);
    connect(this, &StudentWindow::orderRequested, controller_, &PrintController::submitOrder, Qt::QueuedConnection);
    connect(this, &StudentWindow::cancelRequested, controller_, &PrintController::cancel, Qt::QueuedConnection);
    connect(this, &StudentWindow::closeRequested, controller_, &PrintController::requestClose, Qt::QueuedConnection);
    connect(controller_, &PrintController::quoteReady, this, &StudentWindow::applyQuote, Qt::QueuedConnection);
    connect(controller_, &PrintController::orderResult, this, &StudentWindow::applyOrderResult, Qt::QueuedConnection);
    connect(controller_, &PrintController::viewChanged, this, &StudentWindow::applyView, Qt::QueuedConnection);
    connect(controller_, &PrintController::feedback, this, [this](const QString& message, bool problem) {
        // Accepted orders are worded by applyOrderResult; other confirmations use the pending action's text.
        if (problem) ui::say(feedback_, message, "error");
        else if (!message.contains(QStringLiteral("已确认"))) ui::say(feedback_, message);
        else if (!successMessage_.isEmpty()) ui::say(feedback_, successMessage_, "ready");
        successMessage_.clear();
    }, Qt::QueuedConnection);
    connect(controller_, &PrintController::closeReady, this, [this](bool allowed) {
        closePending_ = false; centralWidget()->setEnabled(true);
        if (allowed) { closeApproved_ = true; close(); }
    }, Qt::QueuedConnection);
    clearDocument();
    updatePrice();
    backendThread_.start();
}
StudentWindow::~StudentWindow() { backendThread_.quit(); backendThread_.wait(); }

QString StudentWindow::student() const { return student_->text().trimmed(); }
QJsonObject StudentWindow::order() const {
    const auto title = title_->text().trimmed();
    return {{"title", title.isEmpty() ? QStringLiteral("未命名文档") : title}, {"pages", pageCount_->value()}, {"copies", copies_->value()},
        {"paper", paper_->currentIndex() == 1 ? "a3" : "a4"}, {"color", color_->isChecked()}, {"duplex", duplex_->isChecked()},
        {"service", QStringList{"express", "standard", "flexible"}[service_->checkedId()]}};
}
void StudentWindow::setDocument(const QString& path) {
    const auto document = inspectDocument(path);
    if (!document.error.isEmpty()) { ui::say(feedback_, QStringLiteral("无法使用这个文件：") + document.error, "error"); return; }
    documentChosen_ = true;
    title_->setText(document.name);
    if (document.pages > 0) pageCount_->setValue(document.pages);
    fileName_->setText(shortName(document.name)); fileName_->setToolTip(document.name);
    file_->setText(QStringLiteral("%1 · %2").arg(sizeText(document.bytes), document.pages > 0
        ? QStringLiteral("识别到 %1 页，可在下方修改").arg(document.pages) : QStringLiteral("未能识别页数，请在下方填写")));
    choose_->setText(QStringLiteral("更换文件")); remove_->show(); ui::restyle(drop_, "state", "filled");
    ui::say(feedback_, QStringLiteral("已登记文件。确认打印设置和价格后支付。"));
    pages_->setCurrentIndex(0);
    updatePrice();
}
void StudentWindow::clearDocument() {
    documentChosen_ = false;
    fileName_->setText(QStringLiteral("把文件拖进窗口，或点击右侧选择")); fileName_->setToolTip({});
    file_->setText(QStringLiteral("模拟上传：只登记文件名、大小和页数，不保存也不传输文件内容。"));
    choose_->setText(QStringLiteral("选择文件")); remove_->hide(); ui::restyle(drop_, "state", "");
    title_->clear();
    updateActions();
}
// The local price is only an instant preview; the backend quote that follows is authoritative.
void StudentWindow::updatePrice() {
    const bool valid = pageCount_->value() * copies_->value() <= 10000;
    Job job; job.pages = pageCount_->value(); job.copies = copies_->value();
    job.paper = paper_->currentIndex() == 1 ? PaperSize::A3 : PaperSize::A4;
    job.color = color_->isChecked(); job.duplex = duplex_->isChecked();
    const QStringList tiers{QStringLiteral("快速"), QStringLiteral("普通"), QStringLiteral("灵活")};
    const QStringList windows{QStringLiteral("30 分钟内取件"), QStringLiteral("2 小时内取件"), QStringLiteral("8 小时内取件")};
    const QStringList discounts{QStringLiteral("原价"), QStringLiteral("95 折"), QStringLiteral("85 折")};
    for (int tier = 0; tier < 3; ++tier) {
        job.service = static_cast<ServiceTier>(tier);
        if (valid) quote(job);
        service_->button(tier)->setText(tiers[tier] + (valid ? QStringLiteral("  ") + text::money(job.quotedCents) : QString()) +
            "\n" + windows[tier] + QStringLiteral(" · ") + discounts[tier]);
    }
    if (!valid) {
        price_->setText(QStringLiteral("—")); ui::say(priceDetail_, QStringLiteral("总面数（页数 × 份数）不得超过 10000"), "error");
        quoteAccepted_ = false; quotePending_ = false; ++quoteToken_; updateActions(); return;
    }
    job.service = static_cast<ServiceTier>(service_->checkedId()); quote(job);
    quotedCents_ = job.quotedCents;
    price_->setText(text::money(job.quotedCents));
    ui::say(priceDetail_, QStringLiteral("%1 面 · %2 张纸 · 原价 %3 · %4").arg(job.sheets()).arg(job.paperSheets())
        .arg(text::money(job.baseCents), discounts[service_->checkedId()]));
    ++quoteToken_; // a reply to the previous options must not overwrite this preview
    quotePending_ = true;
    quoteDelay_->start();
    updateActions();
}
void StudentWindow::applyQuote(int token, const QJsonObject& quote) {
    if (token != quoteToken_) return;
    quotePending_ = false;
    quoteAccepted_ = quote.value("type").toString() == QLatin1String("quote");
    if (quoteAccepted_) {
        quotedCents_ = static_cast<Money>(quote.value("quoted_cents").toDouble());
        price_->setText(text::money(quotedCents_));
        deadline_->setText(QStringLiteral("承诺 %1 前可取件。预计时间按当前队列推算，不含之后到达的订单。")
            .arg(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(quote.value("deadline_ms").toDouble())).toString("MM-dd HH:mm")));
    } else deadline_->clear();
    deadline_->setVisible(quoteAccepted_);
    eta_->setText(text::quoteForecast(quote));
    const bool known = quote.value("estimated_finish_ms").isDouble();
    tone(eta_, !quoteAccepted_ ? "error" : !known || quote.value("paused").toBool() ? "waiting" : "ready");
    updateActions();
}
void StudentWindow::updateActions() {
    // The first unmet condition, in the order the student can fix them.
    const QString blocker = !view_.connected ? QStringLiteral("店铺暂时连不上，恢复连接后即可支付。")
        : !view_.shop.value("shop_enabled").toBool() ? QStringLiteral("这个数据目录不是经营模式，无法下单。")
        : !documentChosen_ ? QStringLiteral("先选择要打印的文件。")
        : title_->text().trimmed().isEmpty() ? QStringLiteral("请填写文档名称。")
        : student().isEmpty() ? QStringLiteral("请先在右上角填写学号。")
        : quoteAccepted_ ? QString()
        : quotePending_ ? QStringLiteral("正在向店铺确认报价…") : QStringLiteral("当前设置暂时无法下单，原因见上方说明。");
    pay_->setEnabled(blocker.isEmpty());
    pay_->setText(quoteAccepted_ ? QStringLiteral("支付 %1 并提交订单").arg(text::money(quotedCents_)) : QStringLiteral("支付并提交订单"));
    pay_->setToolTip(blocker);
    payHint_->setText(blocker); payHint_->setVisible(!blocker.isEmpty());
    const int row = orders_->currentRow();
    bool active = false;
    if (row >= 0 && row < orderIds_.size()) for (const auto& job : view_.jobs)
        if (job.id == orderIds_[row]) active = job.state == JobState::Queued || job.state == JobState::Running;
    cancel_->setEnabled(view_.connected && active);
    cancel_->setToolTip(active ? QStringLiteral("取消选中的订单，已付金额按原路退回（模拟）") : QStringLiteral("选中一个等待中或打印中的订单后可取消"));
}
void StudentWindow::showPayment() {
    if (paymentDialog_) return;
    const auto method = static_cast<PaymentMethod>(payment_->checkedId());
    auto* dialog = new QDialog(this); dialog->setObjectName("paymentDialog"); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("确认支付（模拟）")); dialog->setWindowModality(Qt::WindowModal); dialog->setFixedWidth(440);
    auto* body = new QVBoxLayout(dialog); body->setContentsMargins(28, 24, 28, 22); body->setSpacing(10);
    body->addWidget(label(text::payment(method) + QStringLiteral("支付"), "section"));
    body->addWidget(label(QStringLiteral("这是模拟支付，不会产生真实扣款。"), "muted"));
    body->addWidget(label(text::money(quotedCents_), "price"));
    auto* summary = card("summary"); auto* rows = new QGridLayout(summary); rows->setContentsMargins(14, 12, 14, 12);
    rows->setHorizontalSpacing(14); rows->setVerticalSpacing(7); rows->setColumnStretch(1, 1);
    const QStringList captions{QStringLiteral("文档"), QStringLiteral("规格"), QStringLiteral("费用"), QStringLiteral("预计")};
    const QStringList values{shortName(title_->text().trimmed()),
        QStringLiteral("%1 %2 %3 · %4 页 × %5 份").arg(paper_->currentText(), color_->isChecked() ? QStringLiteral("彩色") : QStringLiteral("黑白"),
            duplex_->isChecked() ? QStringLiteral("双面") : QStringLiteral("单面")).arg(pageCount_->value()).arg(copies_->value()),
        priceDetail_->text(), eta_->text()};
    for (int i = 0; i < captions.size(); ++i) {
        rows->addWidget(label(captions[i], "muted"), i, 0, Qt::AlignTop);
        auto* value = label(values[i], "body"); value->setWordWrap(true); value->setMinimumWidth(300); rows->addWidget(value, i, 1);
    }
    body->addWidget(summary);
    auto* status = label({}, "muted"); status->setObjectName("paymentStatus"); status->setWordWrap(true); status->setMinimumWidth(440 - 2 * 28); body->addWidget(status);
    auto* buttons = new QHBoxLayout; buttons->setSpacing(10);
    auto* back = new QPushButton(QStringLiteral("返回修改")); back->setObjectName("cancelPayment"); back->setAutoDefault(false); buttons->addWidget(back, 1);
    auto* confirm = new QPushButton(QStringLiteral("确认支付")); confirm->setObjectName("confirmPayment"); confirm->setProperty("role", "primary");
    confirm->setDefault(true); buttons->addWidget(confirm, 1); body->addLayout(buttons);
    connect(back, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, [this, confirm, back, status, method] {
        confirm->setEnabled(false); back->setEnabled(false); ui::say(status, QStringLiteral("支付处理中…"));
        auto request = order(); request.insert("payment", paymentName(method)); request.insert("customer", student());
        emit orderRequested(request);
    });
    paymentDialog_ = dialog; dialog->open();
}
void StudentWindow::applyOrderResult(bool accepted, const QString& id, const QString& message) {
    if (!accepted) {
        const auto reason = QStringLiteral("订单未提交，未扣款：") + message;
        ui::say(feedback_, reason, "error");
        if (!paymentDialog_) return;
        ui::say(paymentDialog_->findChild<QLabel*>("paymentStatus"), reason, "error");
        paymentDialog_->findChild<QPushButton*>("confirmPayment")->setEnabled(true);
        paymentDialog_->findChild<QPushButton*>("cancelPayment")->setEnabled(true);
        return;
    }
    if (paymentDialog_) paymentDialog_->accept();
    ui::say(feedback_, QStringLiteral("支付成功（模拟），订单已进入队列。取件码 %1").arg(text::pickupCode(id)), "ready");
    clearDocument();
    pendingSelect_ = id; // show the student where the order they just paid for is
    pages_->setCurrentIndex(1);
    applyView(view_);
}
void StudentWindow::applyView(const ViewState& view) {
    const bool connectedNow = view.connected && !view_.connected;
    view_ = view;
    if (view.connected) emit backendReady();
    const bool shop = view.shop.value("shop_enabled").toBool();
    connection_->setText(!view.connected ? QStringLiteral("店铺暂时连不上") : !shop ? QStringLiteral("需要经营模式")
        : view.paused ? QStringLiteral("店铺暂停派发") : QStringLiteral("店铺营业中"));
    connection_->setToolTip(view.connection);
    tone(connection_, !view.connected || !shop ? "error" : view.paused ? "waiting" : "ready");
    if (view.connected && !shop) ui::say(feedback_, QStringLiteral("这个数据目录是旧版同构队列；学生端需要经营模式的数据目录。"), "error");
    if (connectedNow) quoteDelay_->start();

    const auto me = student();
    const auto selected = orders_->currentRow() >= 0 && orders_->currentRow() < orderIds_.size() ? orderIds_[orders_->currentRow()] : QString();
    std::vector<const JobView*> mine, waiting;
    for (auto job = view_.jobs.crbegin(); job != view_.jobs.crend(); ++job) if (!me.isEmpty() && job->customer == me) mine.push_back(&*job);
    for (const auto& job : view_.jobs) if (job.state == JobState::Queued) waiting.push_back(&job);
    int active = 0, ready = 0;
    const JobView* soonest = nullptr;
    orderIds_.clear();
    {
        const QSignalBlocker blocker(orders_);
        orders_->setRowCount(static_cast<int>(mine.size()));
        for (int row = 0; row < static_cast<int>(mine.size()); ++row) {
            const auto& job = *mine[static_cast<std::size_t>(row)];
            orderIds_ << job.id;
            const bool live = job.state == JobState::Queued || job.state == JobState::Running || job.state == JobState::Cancelling;
            if (live) ++active;
            if (job.state == JobState::Completed) ++ready;
            if (live && job.estimatedFinishMs >= 0 && (!soonest || job.estimatedFinishMs < soonest->estimatedFinishMs)) soonest = &job;
            const bool refunded = job.state == JobState::Cancelled || job.state == JobState::Failed;
            const auto paid = refunded ? QStringLiteral("已原路退款") : job.refundCents > 0
                ? QStringLiteral("已退款 %1").arg(text::money(job.refundCents)) : text::payment(job.payment) + QStringLiteral("已支付");
            QString progress = text::forecast(job, text::forecastNow(view_), view_.paused);
            if (job.state == JobState::Queued && job.estimatedFinishMs >= 0 && !view_.paused)
                progress = QStringLiteral("第 %1 位 · %2 · 预计 %3 完成").arg(job.ahead + 1)
                    .arg(text::startsIn(job.estimatedStartMs - view_.nowMs), text::clock(job.estimatedFinishMs));
            else if (job.state == JobState::Running && job.estimatedFinishMs >= 0)
                progress += QStringLiteral("（还需%1）").arg(text::duration(job.estimatedFinishMs - view_.nowMs));
            else if (job.state == JobState::Completed) progress = QStringLiteral("已完成，可凭取件码取件");
            else if (job.state == JobState::Cancelling) progress = QStringLiteral("正在停止打印");
            else if (job.state == JobState::Failed) progress = QStringLiteral("打印失败");
            else if (job.state == JobState::Cancelled) progress = QStringLiteral("已取消");
            else if (progress.isEmpty()) progress = QStringLiteral("暂无法预计");
            const bool done = job.state == JobState::Completed;
            const QList<ui::Cell> cells{{job.title, "", -1, "strong"}, job.specification + QStringLiteral(" · %1 面").arg(job.sheets),
                text::money(job.quotedCents) + QStringLiteral(" · ") + paid, {text::state(job.state), text::tone(job.state)}, progress,
                {QStringLiteral("%1 / %2 面").arg(job.printed).arg(job.sheets), "", job.sheets > 0 ? double(job.printed) / job.sheets : 0.},
                {done ? text::pickupCode(job.id) : QStringLiteral("—"), "", -1, done ? "accent" : "muted"}};
            for (int column = 0; column < cells.size(); ++column) ui::setCell(orders_, row, column, cells[column]);
        }
        int row = pendingSelect_.isEmpty() ? -1 : static_cast<int>(orderIds_.indexOf(pendingSelect_));
        if (row >= 0) pendingSelect_.clear();
        else row = static_cast<int>(orderIds_.indexOf(selected));
        if (row != orders_->currentRow()) orders_->setCurrentCell(row, 0);
    }
    ordersEmpty_->setVisible(mine.empty());
    ordersSummary_->setText(me.isEmpty() ? QStringLiteral("填写学号后显示订单") : QStringLiteral("学号 %1 · 共 %2 单").arg(me).arg(mine.size()));

    const auto fleet = view_.shop.value("fleet").toArray();
    int printing = 0;
    if (devices_->rowCount() != static_cast<int>(view_.printers.size())) {
        devices_->setRowCount(static_cast<int>(view_.printers.size()));
        ui::fitRows(devices_);
    }
    for (int i = 0; i < static_cast<int>(view_.printers.size()); ++i) {
        const auto object = fleet[i].toObject();
        const auto& printer = view_.printers[static_cast<std::size_t>(i)];
        const bool busy = !printer.jobId.isEmpty(), online = object.value("online").toBool(true);
        if (busy) ++printing;
        const QString status = (busy ? QStringLiteral("打印中 %1%").arg(printer.total > 0 ? printer.printed * 100 / printer.total : 0) +
                (!me.isEmpty() && printer.customer == me ? QStringLiteral(" · 我的订单") : QString()) : QStringLiteral("空闲")) +
            (online ? QString() : QStringLiteral(" · 暂停接新单"));
        const bool timed = busy && printer.estimatedFinishMs >= 0;
        const QList<ui::Cell> cells{{text::deviceName(object, i), "", -1, "strong"}, text::capability(object),
            {status, !online ? "waiting" : busy ? "info" : "ready"},
            {timed ? text::clock(printer.estimatedFinishMs) : QStringLiteral("—"), "", -1, timed ? "" : "muted"}};
        for (int column = 0; column < cells.size(); ++column) ui::setCell(devices_, i, column, cells[column]);
    }
    // Forecast order first; orders that cannot be estimated go last.
    std::stable_sort(waiting.begin(), waiting.end(), [](const JobView* a, const JobView* b) {
        const bool knownA = a->estimatedFinishMs >= 0, knownB = b->estimatedFinishMs >= 0;
        return knownA != knownB ? knownA : knownA && a->ahead < b->ahead;
    });
    const int shown = std::min(50, static_cast<int>(waiting.size()));
    queue_->setRowCount(shown);
    queueEmpty_->setVisible(shown == 0);
    for (int row = 0; row < shown; ++row) {
        const auto& job = *waiting[static_cast<std::size_t>(row)];
        const bool known = job.estimatedFinishMs >= 0, own = !me.isEmpty() && job.customer == me;
        const QList<ui::Cell> cells{{QString::number(row + 1), "", -1, "muted"},
            {own ? QStringLiteral("我的订单 · ") + job.title : QStringLiteral("其他订单"), "", -1, own ? "accent" : ""},
            job.specification + QStringLiteral(" · %1 面").arg(job.sheets),
            !known ? text::blocked(job.blocked) : view_.paused ? text::afterResume(job.estimatedStartMs - text::forecastNow(view_))
                : text::clock(job.estimatedStartMs),
            known && !view_.paused ? text::clock(job.estimatedFinishMs) : QStringLiteral("—")};
        for (int column = 0; column < cells.size(); ++column) ui::setCell(queue_, row, column, cells[column]);
    }

    metricValues_[0]->setText(QStringLiteral("%1 单").arg(waiting.size()));
    metricHints_[0]->setText(QStringLiteral("%1 / %2 台设备正在打印").arg(printing).arg(view_.printers.size()));
    metricValues_[1]->setText(view_.queueClearMs < 0 ? (waiting.empty() && printing == 0 ? QStringLiteral("无需等待") : QStringLiteral("暂无法预计"))
        : text::duration(view_.queueClearMs - text::forecastNow(view_)));
    metricHints_[1]->setText(view_.paused ? QStringLiteral("店铺暂停派发，恢复后开始计时") : QStringLiteral("按当前队列推算，不含新订单"));
    metricValues_[2]->setText(QStringLiteral("%1 单").arg(active));
    metricHints_[2]->setText(!soonest ? QStringLiteral("暂无可预计的进行中订单") : view_.paused ? QStringLiteral("店铺暂停派发，恢复后继续")
        : QStringLiteral("最近一单预计 %1 完成").arg(text::clock(soonest->estimatedFinishMs)));
    metricValues_[3]->setText(QStringLiteral("%1 单").arg(ready));
    metricHints_[3]->setText(QStringLiteral("凭“我的订单”里的取件码到店取件"));
    tone(metricValues_[3], ready > 0 ? "ready" : "");
    updateActions();
}
void StudentWindow::dragEnterEvent(QDragEnterEvent* event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) return;
    pages_->setCurrentIndex(0); ui::restyle(drop_, "state", "active");
    event->acceptProposedAction();
}
void StudentWindow::dragLeaveEvent(QDragLeaveEvent*) { ui::restyle(drop_, "state", documentChosen_ ? "filled" : ""); }
void StudentWindow::dropEvent(QDropEvent* event) {
    ui::restyle(drop_, "state", documentChosen_ ? "filled" : "");
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) return;
    setDocument(urls.first().toLocalFile());
    event->acceptProposedAction();
}
void StudentWindow::closeEvent(QCloseEvent* event) {
    if (closeApproved_) { event->accept(); return; }
    event->ignore();
    if (closePending_) return;
    closePending_ = true; centralWidget()->setEnabled(false); emit closeRequested();
}
} // namespace cps::gui
