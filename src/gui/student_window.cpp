#include "student_window.hpp"
#include "office_preview.hpp"
#include "ui_theme.hpp"
#include "editorial_surface.hpp"
#include "editorial_chrome.hpp"
#include "payment_icon.hpp"
#include "payment_receipt.hpp"
#include "editorial_color_switch.hpp"
#include <QResource>
#include <QFontDatabase>
#include "print_proof.hpp"
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

static void initializeEditorialResources() {
    Q_INIT_RESOURCE(editorial_assets);
#ifdef Q_OS_WIN
    // Qt's offscreen platform does not enumerate Windows system fonts. Register
    // the same installed families so evidence captures use real CJK glyphs too.
    static bool registered = false;
    if (!registered && QGuiApplication::platformName() == QLatin1String("offscreen")) {
        const auto fonts = QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts");
        for (const auto &name : {"msyh.ttc", "msyhbd.ttc", "simsun.ttc", "segoeui.ttf"})
            QFontDatabase::addApplicationFont(QDir(fonts).filePath(name));
        registered = true;
    }
#endif
}

namespace cps::gui {
namespace {
using ui::card;
using ui::label;
using ui::tone;
constexpr qint64 MaxDocumentBytes = 64 * 1024 * 1024;

// The outer editorial canvas scales the strip's geometry. Its layout-managed
// labels must use that same scale, rather than compressing three fixed-size
// text rows into a shorter widget at compact window sizes.
class StudentMetricStrip : public QWidget {
  protected:
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        const qreal scale = std::min(width() / 1498., height() / 100.);
        layout()->setSpacing(std::max(2, qRound(6 * scale)));
        for (auto *box : findChildren<QFrame *>(QString(), Qt::FindDirectChildrenOnly)) {
            box->layout()->setContentsMargins(qRound(16 * scale), std::max(2, qRound(4 * scale)),
                                              qRound(12 * scale), std::max(2, qRound(4 * scale)));
            box->layout()->setSpacing(std::max(2, qRound(4 * scale)));
            for (auto *text : box->findChildren<QLabel *>()) {
                const int referenceSize = text->property("role") == "metricValue" ? 26 : 12;
                text->setStyleSheet(QStringLiteral("font-size:%1px;")
                    .arg(std::max(10, qRound(referenceSize * scale))));
                text->ensurePolished();
            }
        }
    }
};

QString sizeText(qint64 bytes) {
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / 1048576.0, 0, 'f', 1);
}
// Long names keep both ends: the start identifies the document, the end its type.
QString shortName(const QString &name) {
    return name.size() <= 44 ? name : name.left(26) + QStringLiteral("…") + name.right(14);
}
bool startsAt(const QByteArray &data, qsizetype at, const char *word) {
    const auto length = static_cast<qsizetype>(std::strlen(word));
    return at >= 0 && at + length <= data.size() &&
           std::memcmp(data.constData() + at, word, static_cast<std::size_t>(length)) == 0;
}
qsizetype skipSpace(const QByteArray &data, qsizetype at) {
    while (at < data.size() && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n'))
        ++at;
    return at;
}
// Best effort without a PDF library: count page objects, or fall back to the page
// tree's /Count when the objects sit inside compressed streams.
int pdfPages(const QByteArray &data) {
    int pages = 0;
    for (auto at = data.indexOf("/Type"); at >= 0; at = data.indexOf("/Type", at + 5)) {
        const auto name = skipSpace(data, at + 5);
        if (!startsAt(data, name, "/Page"))
            continue;
        const char next = name + 5 < data.size() ? data[name + 5] : ' ';
        if (!((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')))
            ++pages; // not /Pages
    }
    if (pages > 0)
        return pages;
    for (auto at = data.indexOf("/Count"); at >= 0; at = data.indexOf("/Count", at + 6)) {
        auto digit = skipSpace(data, at + 6);
        int count = 0;
        while (digit < data.size() && data[digit] >= '0' && data[digit] <= '9' && count < 100000)
            count = count * 10 + (data[digit++] - '0');
        pages = std::max(pages, count);
    }
    return pages;
}
} // namespace

DocumentInfo inspectDocument(const QString &path) {
    const QFileInfo info(path);
    DocumentInfo document;
    document.name = info.fileName().left(200);
    document.bytes = info.size();
    QFile file(path);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
        document.error = QStringLiteral("文件无法读取");
        return document;
    }
    if (document.bytes == 0) {
        document.error = QStringLiteral("文件是空的");
        return document;
    }
    if (document.bytes > MaxDocumentBytes) {
        document.error = QStringLiteral("文件超过 64 MB");
        return document;
    }
    const auto data = file.readAll();
    const auto suffix = info.suffix().toLower();
    if (data.startsWith("%PDF"))
        document.pages = pdfPages(data);
    else if (QStringList{"txt", "md", "csv", "log", "c", "h", "cpp", "hpp", "py", "java", "json", "tex"}
                 .contains(suffix))
        document.pages = static_cast<int>((data.count('\n') + 50) / 50); // about 50 lines per page
    else if (QStringList{"png", "jpg", "jpeg", "bmp", "gif"}.contains(suffix))
        document.pages = 1;
    document.pages = std::clamp(document.pages, 0, 1000);
    return document;
}

StudentWindow::StudentWindow(WindowOptions options, const QString &studentId, QWidget *parent)
    : QMainWindow(parent) {
    paymentCodesDirectory_ = QDir(options.stateDirectory).filePath("payment-codes");
    setWindowTitle(QStringLiteral("纸间 · 俯视印刷编辑台"));
    setMinimumSize(920, 640);
    const auto available = screen()->availableGeometry();
    resize(std::min(1420, available.width() - 40), std::min(888, available.height() - 60));
    setAcceptDrops(true);
    initializeEditorialResources();
    ui::applyTheme(*this);
    QApplication::setWindowIcon(editorialBrandIcon());
    new EditorialChrome(this, 55, 992);
    auto *central = new EditorialSurface(this);
    central->setStyleSheet(QStringLiteral(
        "QWidget#editorialSurface{background:#FAFAF9;} "
        "QStackedWidget,QScrollArea{background:transparent;border:none;} "
        "QPushButton{color:#14213E;background:#FFFFFF;border:1px solid #D9DCE2;border-radius:6px;padding:0;} "
        "QPushButton:hover{background:#FFF6F2;border-color:#D6402C;} QPushButton:focus{border:2px solid "
        "#D6402C;} QPushButton:checked{color:#D6402C;background:#FFF9F5;border-color:#D6402C;} "
        "QPushButton:disabled{color:#A2A7B0;} QLineEdit,QSpinBox{color:#14213E;background:white;border:1px "
        "solid #D9DCE2;border-radius:4px;padding:0 8px;} "
        "QLabel[tone=ready]{color:#B93424;} QLabel[tone=waiting]{color:#786757;} "
        "QLabel[tone=error]{color:#9E3025;} "
        "QFrame[role=metric]{background:transparent;border:none;border-left:2px solid #E5E2DC;} "
        "QTableWidget{background:white;color:#14213E;selection-background-color:#FFF2EC;"
        "selection-color:#14213E;alternate-background-color:#FAF8F4;border:none;} "
        "QHeaderView::section{background:#F3F1EC;color:#69748A;border:none;"
        "border-bottom:1px solid #E5E2DC;padding:12px 10px;} "
        "QTableCornerButton::section{background:#F3F1EC;border:none;} "
        "QScrollBar::handle{background:#B5B2AB;} "
        "QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border-color:#D63D29;} "
        "QLineEdit,QSpinBox,QComboBox{selection-background-color:#F4D9D1;selection-color:#14213E;}"));
    auto put = [central](QWidget *w, QRectF r, int size = 16, QString style = {}) {
        central->place(w, r, size, style);
    };
    const QString ink = "color:#14213E;background:transparent;",
                  muted = "color:#69748A;background:transparent;";
    put(editorialLabel(QStringLiteral("纸间"), 24, true), {92, 8, 100, 38}, 24, ink + "font-weight:700;");
    auto *pageTitle = editorialLabel(QStringLiteral("好作品，值得印出来。"), 40, true);
    pageTitle->setObjectName("studentPageTitle");
    put(pageTitle, {44, 87, 1120, 80}, 40, ink + "font-weight:700;");
    pageTitle->hide();
    auto *navigation = new QButtonGroup(this);
    navigation->setExclusive(true);
    pages_ = new QStackedWidget;
    pages_->setObjectName("studentPages");
    put(pages_, {0, 55, 1586, 787}, 0);
    const QStringList names{QStringLiteral("下单打印"), QStringLiteral("我的订单"),
                            QStringLiteral("排队情况")};
    const QStringList navIds{"navOrder", "navMine", "navQueue"};
    for (int i = 0; i < 3; ++i) {
        auto *b = new QPushButton(names[i]);
        b->setObjectName(navIds[i]);
        b->setCheckable(true);
        b->setChecked(i == 0);
        navigation->addButton(b, i);
        put(b, {620. + 130 * i, 3, 85, 47}, 18, "border:none;background:transparent;color:#17233E;");
        connect(b, &QPushButton::clicked, this, [this, i] { pages_->setCurrentIndex(i); });
        connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this),
                &QShortcut::activated, this, [this, i] { pages_->setCurrentIndex(i); });
    }
    put(editorialLabel(QStringLiteral("学号")), {1211, 10, 42, 34}, 16, ink);
    student_ = new QLineEdit(studentId.trimmed().isEmpty() ? QStringLiteral("2024000001")
                                                           : studentId.trimmed().left(32));
    student_->setObjectName("studentId");
    student_->setMaxLength(32);
    put(student_, {1260, 8, 170, 38}, 16, "border:none;background:transparent;color:#17233E;");
    int control = 0;
    for (auto action : {EditorialWindowButton::Minimize, EditorialWindowButton::Maximize, EditorialWindowButton::Close}) {
        const QString id = action == EditorialWindowButton::Minimize ? "windowMinimize"
                           : action == EditorialWindowButton::Maximize ? "windowMaximize" : "windowClose";
        put(new EditorialWindowButton(this, action, id), {1447. + control++ * 46, 3, 43, 49}, 0);
    }
    auto *metricsBox = new StudentMetricStrip;
    auto *metrics = new QHBoxLayout(metricsBox);
    metrics->setContentsMargins(0, 0, 0, 0);
    const QStringList titles{QStringLiteral("当前排队"), QStringLiteral("预计清空队列"),
                             QStringLiteral("我的进行中订单"), QStringLiteral("可取件")};
    for (int i = 0; i < 4; ++i) {
        auto *box = card("metric");
        auto *m = new QVBoxLayout(box);
        m->addWidget(label(titles[i], "metricLabel"));
        metricValues_[i] = label("—", "metricValue");
        metricValues_[i]->setObjectName(QStringLiteral("studentMetric%1").arg(i));
        m->addWidget(metricValues_[i]);
        metricHints_[i] = label({}, "metricHint");
        m->addWidget(metricHints_[i]);
        metrics->addWidget(box);
    }
    put(metricsBox, {44, 179, 1498, 100}, 0);
    metricsBox->hide();
    connect(pages_, &QStackedWidget::currentChanged, this,
            [this, central, navigation, pageTitle, metricsBox](int index) {
                navigation->button(index)->setChecked(true);
                central->setNavigation(index);
                metricsBox->setVisible(index != 0);
                pageTitle->setVisible(index != 0);
                pageTitle->setText(index == 1 ? QStringLiteral("你的文档，走到哪一步了？")
                                              : QStringLiteral("看看店里现在忙不忙。"));
                central->move(pages_, index == 0 ? QRectF(0, 55, 1586, 787) : QRectF(44, 300, 1498, 529));
            });
    auto *orderArea = new QScrollArea;
    orderArea->setObjectName("orderScroll");
    orderArea->setWidgetResizable(true);
    orderArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    orderArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *orderPage = new EditorialSurface;
    orderPage->setReferenceHeight(787);
    auto at = [orderPage](QWidget *w, QRectF r, int sz = 16, QString style = {}) {
        r.translate(0, -55);
        orderPage->place(w, r, sz, style);
    };
    // The full left surface belongs to the live document scene.
    proof_ = new PrintProof;
    at(proof_, {0, 55, 1218, 787}, 0);
    drop_ = new QFrame;
    drop_->setObjectName("dropZone");
    at(drop_, {1256, 166, 310, 62}, 0);
    drop_->setStyleSheet("QFrame#dropZone{background:white;border:1px solid #EBECEF;border-radius:12px;}"
                        "QFrame#dropZone[state=active]{background:#FFF4EF;border:2px dashed #D63D29;}");
    at(editorialLabel(QStringLiteral("打印设置"), 34, true), {1257, 84, 300, 56}, 34,
       ink + "font-weight:700;");
    fileName_ = editorialLabel({});
    fileName_->setObjectName("fileName");
    fileName_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    at(fileName_, {1301, 181, 185, 32}, 16, ink + "font-weight:600;");
    auto *fileIcon = new QLabel;
    fileIcon->setPixmap(ui::icon(ui::Icon::Document, QColor("#14213E")).pixmap(22, 22));
    fileIcon->setAlignment(Qt::AlignCenter);
    at(fileIcon, {1269, 182, 24, 30}, 0);
    choose_ = new QPushButton(QStringLiteral("选择文件"));
    choose_->setObjectName("chooseFile");
    at(choose_, {1491, 181, 72, 32}, 14, "border:none;background:transparent;color:#1455A1;");
    choose_->setToolTip("Ctrl+O");
    file_ = editorialLabel({});
    file_->setObjectName("fileInfo");
    at(file_, {1257, 232, 240, 24}, 11, muted);
    remove_ = new QPushButton(QStringLiteral("移除"));
    remove_->setObjectName("removeFile");
    at(remove_, {1504, 232, 58, 24}, 11, "border:none;background:transparent;color:#69748A;");
    remove_->hide();
    title_ = new QLineEdit(orderPage);
    title_->setObjectName("orderTitle");
    title_->setMaxLength(200);
    title_->hide();
    pageCount_ = new EditorialNumberInput;
    pageCount_->setObjectName("orderPages");
    pageCount_->setRange(1, 1000);
    pageCount_->setValue(8);
    pageCount_->setAccessibleName(QStringLiteral("打印页数"));
    copies_ = new EditorialNumberInput;
    copies_->setObjectName("orderCopies");
    copies_->setRange(1, 100);
    copies_->setValue(1);
    copies_->setAccessibleName(QStringLiteral("打印份数"));
    at(editorialLabel(QStringLiteral("页数"), 18, true), {1257, 322, 90, 44}, 18, ink + "font-weight:700;");
    at(pageCount_, {1363, 322, 198, 44}, 20);
    at(editorialLabel(QStringLiteral("份数"), 18, true), {1257, 392, 90, 44}, 18, ink + "font-weight:700;");
    at(copies_, {1363, 392, 198, 44}, 20);
    paper_ = new ui::SelectInput(orderPage);
    paper_->setObjectName("orderPaper");
    paper_->addItems({"A4", "A3"});
    paper_->hide();
    at(editorialLabel(QStringLiteral("纸张"), 18, true), {1257, 260, 90, 44}, 18, ink + "font-weight:700;");
    auto *paperChoices = new QButtonGroup(this);
    paperChoices->setExclusive(true);
    for (int i = 0; i < 2; ++i) {
        auto *b = new QPushButton(i == 0 ? "A4" : "A3");
        b->setObjectName(i == 0 ? "paperA4" : "paperA3");
        b->setCheckable(true);
        b->setChecked(i == 0);
        paperChoices->addButton(b, i);
        at(b, {1363. + 106 * i, 258, 92, 41}, 17);
    }
    connect(paperChoices, &QButtonGroup::idClicked, paper_, &QComboBox::setCurrentIndex);
    connect(paper_, &QComboBox::currentIndexChanged, this,
            [paperChoices](int i) { paperChoices->button(i)->setChecked(true); });
    color_ = new ui::CheckBox(QString());
    color_->setObjectName("orderColor");
    color_->setParent(orderPage);
    color_->setChecked(true);
    color_->hide();
    duplex_ = new ui::CheckBox(QString());
    duplex_->setObjectName("orderDuplex");
    duplex_->setParent(orderPage);
    duplex_->setChecked(true);
    duplex_->hide();
    auto *colors = new QButtonGroup(this);
    colors->setExclusive(true);
    auto *colorSwitch = new EditorialColorSwitch;
    at(colorSwitch, {989, 171, 190, 62}, 0);
    for (int i = 0; i < 2; ++i) {
        auto *b = new EditorialColorButton(i == 0 ? QStringLiteral("黑白") : QStringLiteral("彩色"));
        b->setObjectName(i == 0 ? "colorMono" : "colorFull");
        b->setCheckable(true);
        b->setChecked(i == 1);
        colors->addButton(b, i);
        colorSwitch->layout()->addWidget(b);
    }
    connect(colors, &QButtonGroup::idClicked, this, [this](int i) { color_->setChecked(i == 1); });
    connect(color_, &QCheckBox::toggled, this,
            [colors](bool checked) { colors->button(checked ? 1 : 0)->setChecked(true); });
    at(editorialLabel(QStringLiteral("单双面"), 18, true), {1257, 465, 90, 44}, 18, ink + "font-weight:700;");
    auto *sides = new QButtonGroup(this);
    sides->setExclusive(true);
    for (int i = 0; i < 2; ++i) {
        auto *b = new QPushButton(i == 0 ? QStringLiteral("单面打印") : QStringLiteral("双面打印"));
        b->setObjectName(i == 0 ? "sideSingle" : "sideDouble");
        b->setCheckable(true);
        b->setChecked(i == 1);
        sides->addButton(b, i);
        at(b, {1363. + 103 * i, 465, 96, 41}, 16);
    }
    connect(sides, &QButtonGroup::idClicked, this, [this](int i) { duplex_->setChecked(i == 1); });
    connect(duplex_, &QCheckBox::toggled, this,
            [sides](bool checked) { sides->button(checked ? 1 : 0)->setChecked(true); });
    auto *rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    at(rule, {1257, 540, 304, 2}, 0);
    at(editorialLabel(QStringLiteral("打印服务"), 21, true), {1257, 556, 309, 40}, 21,
       ink + "font-weight:700;");
    service_ = new QButtonGroup(this);
    service_->setExclusive(true);
    const QStringList tierIds{"serviceExpress", "serviceStandard", "serviceFlexible"};
    for (int i = 0; i < 3; ++i) {
        auto *b = new EditorialServiceButton;
        b->setObjectName(tierIds[i]);
        b->setCheckable(true);
        b->setChecked(i == 1);
        service_->addButton(b, i);
        at(b, {1257, 605. + 68 * i, 304, 64}, 17,
           "border:none;background:transparent;text-align:left;padding:0 5px;");
    }
    flipProof_ = new EditorialToolButton(QStringLiteral("翻页"));
    flipProof_->setObjectName("flipProof");
    flipProof_->setIcon(ui::icon(ui::Icon::Refresh, QColor("#14213E")));
    auto *unfold = new EditorialToolButton(QStringLiteral("展开"));
    unfold->setObjectName("unfoldProof");
    unfold->setCheckable(true);
    unfold->setChecked(false);
    unfold->setToolTip(QStringLiteral("点击保持展开；再次点击收起。也可悬停纸张临时展开。"));
    unfold->setIcon(ui::icon(ui::Icon::Document, QColor("#14213E")));
    auto *align = new EditorialToolButton(QStringLiteral("对齐"));
    align->setObjectName("alignProof");
    align->setCheckable(true);
    align->setIcon(ui::icon(ui::Icon::Activity, QColor("#14213E")));
    auto *rotate = new EditorialToolButton(QStringLiteral("旋转"));
    rotate->setObjectName("rotateProof");
    rotate->setIcon(ui::icon(ui::Icon::Refresh, QColor("#14213E")));
    rotate->setToolTip(QStringLiteral("顺时针旋转预览 90°，不改变原文件或打印设置。"));
    rotate->setAccessibleName(QStringLiteral("旋转预览"));
    rotate->setEnabled(false);
    for (auto *b : std::array<QPushButton *, 4>{flipProof_, unfold, align, rotate})
        b->setStyleSheet("background:rgba(255,255,255,235);border:none;color:#14213E;border-radius:4px;font-"
                         "size:13px;padding:0;");
    proof_->attachActions(flipProof_, unfold);
    proof_->attachAlignment(align);
    proof_->attachRotation(rotate);
    connect(rotate, &QPushButton::clicked, this, [this] { proof_->rotatePreview(); });
    connect(unfold, &QPushButton::toggled, this, [this, unfold](bool open) {
        proof_->setExpanded(open);
        unfold->setText(open ? QStringLiteral("▤\n收起") : QStringLiteral("▤\n展开"));
    });
    connect(align, &QPushButton::toggled, this, [this](bool yes) { proof_->setAligned(yes); });
    connect(flipProof_, &QPushButton::clicked, this, [this] { proof_->setBack(!proof_->showingBack()); });
    auto *previous = new QPushButton(QStringLiteral("‹"));
    previous->setObjectName("proofPrevious");
    previous->setAccessibleName(QStringLiteral("上一张纸"));
    previous->setToolTip(QStringLiteral("上一张纸（左方向键）"));
    auto *next = new QPushButton(QStringLiteral("›"));
    next->setObjectName("proofNext");
    next->setAccessibleName(QStringLiteral("下一张纸"));
    next->setToolTip(QStringLiteral("下一张纸（右方向键）"));
    at(previous, {1129, 780, 35, 33}, 24);
    at(next, {1171, 780, 35, 33}, 24);
    connect(previous, &QPushButton::clicked, this, [this] { proof_->stepPage(-1); });
    connect(next, &QPushButton::clicked, this, [this] { proof_->stepPage(1); });
    proof_->changed = [this, previous, next, rotate] {
        rotate->setEnabled(proof_->documentPages() > 0);
        previous->setEnabled(proof_->currentSheet() > 1);
        next->setEnabled(proof_->currentSheet() < proof_->sheetCount());
        flipProof_->setText(proof_->showingBack() ? QStringLiteral("▱\n正面") : QStringLiteral("▱\n翻页"));
    };
    auto *magnify = new QPushButton(QStringLiteral("放大预览"));
    magnify->setObjectName("magnifyProof");
    at(magnify, {989, 781, 118, 32}, 13);
    connect(magnify, &QPushButton::clicked, this, [this] { proof_->openReader(); });
    payment_ = new QButtonGroup(this);
    payment_->setExclusive(true);
    for (auto method : {PaymentMethod::Wechat, PaymentMethod::Alipay, PaymentMethod::CampusCard}) {
        auto *b = new QPushButton(text::payment(method), this);
        b->setObjectName("pay_" + paymentName(method));
        b->setCheckable(true);
        b->setChecked(method == PaymentMethod::Wechat);
        payment_->addButton(b, static_cast<int>(method));
        b->hide();
    }
    price_ = editorialLabel(QStringLiteral("¥0.00"), 44, true);
    price_->setObjectName("quotePrice");
    put(price_, {1100, 859, 167, 70}, 44, ink + "font-weight:700;");
    priceDetail_ = editorialLabel({});
    priceDetail_->setObjectName("quoteDetail");
    priceDetail_->hide();
    eta_ = editorialLabel(QStringLiteral("正在连接店铺…"), 21, true);
    eta_->setObjectName("quoteEta");
    eta_->setWordWrap(true);
    put(eta_, {550, 867, 525, 34}, 21, ink + "font-weight:700;");
    deadline_ = editorialLabel({});
    deadline_->setObjectName("quoteDeadline");
    put(deadline_, {550, 905, 520, 21}, 12, muted);
    summary_ = editorialLabel({}, 21, true);
    summary_->setObjectName("printSummary");
    put(summary_, {106, 868, 385, 39}, 21, ink + "font-weight:700;");
    pay_ = new QPushButton(QStringLiteral("确认并下单  →"));
    pay_->setObjectName("payOrder");
    pay_->setEnabled(false);
    put(pay_, {1291, 859, 260, 66}, 24,
        "background:#D63D29;color:white;border:none;border-radius:8px;font-weight:600;");
    payHint_ = editorialLabel({});
    payHint_->setObjectName("payHint");
    put(payHint_, {106, 911, 370, 22}, 12, muted);
    auto *motion = new QPushButton(QStringLiteral("减少动效"));
    motion->setObjectName("studentReduceMotion");
    motion->setCheckable(true);
    put(motion, {1449, 952, 102, 29}, 12);
    connect(motion, &QPushButton::toggled, this, [this](bool reduced) { proof_->setMotion(!reduced); });
    orderArea->setWidget(orderPage);
    pages_->addWidget(orderArea);
    // Page 2: only this student's orders.
    auto *mine = card();
    auto *mineLayout = new QVBoxLayout(mine);
    mineLayout->setContentsMargins(18, 16, 18, 14);
    mineLayout->setSpacing(10);
    auto *mineHead = new QHBoxLayout;
    mineHead->setSpacing(8);
    mineHead->addWidget(label(QStringLiteral("我的订单"), "section"));
    ordersSummary_ = label({}, "muted");
    mineHead->addWidget(ordersSummary_);
    mineHead->addStretch();
    cancel_ = new QPushButton(QStringLiteral("取消订单并退款"));
    cancel_->setObjectName("cancelOrder");
    cancel_->setProperty("role", "danger");
    cancel_->setEnabled(false);
    mineHead->addWidget(cancel_);
    mineLayout->addLayout(mineHead);
    orders_ = ui::listTable("myOrders",
                            {QStringLiteral("文档"), QStringLiteral("规格"), QStringLiteral("金额 / 支付"),
                             QStringLiteral("状态"), QStringLiteral("排队与预计时间"), QStringLiteral("进度"),
                             QStringLiteral("取件码")}, true);
    // Only the document name gives way; prices, states and estimates are never elided.
    orders_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    orders_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mineLayout->addWidget(orders_, 1);
    ordersEmpty_ = ui::emptyState(orders_, QStringLiteral("还没有订单"),
                                  QStringLiteral("在“下单打印”上传文件并支付后，订单会出现在这里"));
    auto *mineNote = label(QStringLiteral("预计时间按当前队列、调度策略和设备推算，不含之后到达的订单，仅供参"
                                          "考；承诺期限以订单的取件期限为准。"),
                           "muted");
    mineNote->setWordWrap(true);
    mineLayout->addWidget(mineNote);
    pages_->addWidget(mine);

    // Page 3: the shop as a student may see it, without other students' document names.
    auto *shopArea = new QScrollArea;
    shopArea->setObjectName("queueScroll");
    shopArea->setWidgetResizable(true);
    shopArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *shop = new QWidget;
    shop->setObjectName("queueContent");
    auto *shopLayout = new QVBoxLayout(shop);
    shopLayout->setContentsMargins(0, 0, 0, 0);
    shopLayout->setSpacing(14);
    auto *deviceCard = card();
    auto *deviceLayout = new QVBoxLayout(deviceCard);
    deviceLayout->setContentsMargins(18, 16, 18, 14);
    deviceLayout->setSpacing(10);
    deviceLayout->addWidget(label(QStringLiteral("设备状态"), "section"));
    devices_ = ui::listTable("shopDevices", {QStringLiteral("设备"), QStringLiteral("能力"),
                                             QStringLiteral("状态"), QStringLiteral("预计空闲")}, true);
    devices_->setSelectionMode(QAbstractItemView::NoSelection);
    deviceLayout->addWidget(devices_);
    shopLayout->addWidget(deviceCard);
    auto *queueCard = card();
    auto *queueLayout = new QVBoxLayout(queueCard);
    queueLayout->setContentsMargins(18, 16, 18, 14);
    queueLayout->setSpacing(10);
    queueLayout->addWidget(label(QStringLiteral("等待队列"), "section"));
    queue_ =
        ui::listTable("shopQueue", {QStringLiteral("顺位"), QStringLiteral("订单"), QStringLiteral("规格"),
                                    QStringLiteral("预计开始"), QStringLiteral("预计完成")}, true);
    queue_->setSelectionMode(QAbstractItemView::NoSelection);
    queue_->setMinimumHeight(180);
    queueLayout->addWidget(queue_, 1);
    queueEmpty_ = ui::emptyState(queue_, QStringLiteral("现在不用排队"),
                                 QStringLiteral("有订单等待时，这里按预计开始的先后列出"));
    auto *queueNote = label(QStringLiteral("为保护隐私，不显示其他同学的文档名称。顺位是按当前策略推算的开始"
                                           "顺序，新订单或设备变化都可能改变它。"),
                            "muted");
    queueNote->setWordWrap(true);
    queueLayout->addWidget(queueNote);
    shopLayout->addWidget(queueCard, 1);
    shopArea->setWidget(shop);
    pages_->addWidget(shopArea);

    connection_ = editorialLabel(QStringLiteral("正在连接店铺…"));
    connection_->setObjectName("connection");
    auto *shopStatus = editorialLabel(QStringLiteral("●"));
    shopStatus->setObjectName("shopStatusDot");
    put(shopStatus, {36, 949, 20, 32}, 14, "color:#8C93A1;background:transparent;");
    put(connection_, {63, 949, 185, 32}, 14, ink);
    feedback_ = editorialLabel(QStringLiteral("模拟支付，不会扣款"));
    feedback_->setObjectName("feedback");
    put(feedback_, {263, 949, 1106, 32}, 13, muted);
    setCentralWidget(central);
    quoteDelay_ = new QTimer(this);
    quoteDelay_->setSingleShot(true);
    quoteDelay_->setInterval(150);
    connect(quoteDelay_, &QTimer::timeout, this, [this] {
        if (view_.connected)
            emit quoteRequested(order(), ++quoteToken_);
    });
    // The queue moves even when the form does not, so keep the shown estimate fresh.
    auto *quoteRefresh = new QTimer(this);
    connect(quoteRefresh, &QTimer::timeout, this, [this] {
        if (pages_->currentIndex() == 0 && !paymentDialog_)
            quoteDelay_->start();
    });
    quoteRefresh->start(2000);
    connect(pageCount_, &QSpinBox::valueChanged, this, &StudentWindow::updatePrice);
    connect(copies_, &QSpinBox::valueChanged, this, &StudentWindow::updatePrice);
    connect(paper_, &QComboBox::currentIndexChanged, this, &StudentWindow::updatePrice);
    connect(color_, &QCheckBox::toggled, this, &StudentWindow::updatePrice);
    connect(duplex_, &QCheckBox::toggled, this, &StudentWindow::updatePrice);
    connect(service_, &QButtonGroup::idClicked, this, &StudentWindow::updatePrice);
    connect(title_, &QLineEdit::textChanged, this, [this] {
        updateProof();
        updateActions();
    });
    connect(student_, &QLineEdit::textChanged, this, [this] { applyView(view_); });
    connect(choose_, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择要打印的文件"), {},
            QStringLiteral(
                "文档 (*.pdf *.txt *.md *.doc *.docx *.ppt *.pptx *.xls *.xlsx *.png *.jpg *.jpeg);;所有文件 (*)"));
        if (!path.isEmpty())
            setDocument(path);
    });
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, [this] {
        pages_->setCurrentIndex(0);
        choose_->click();
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        clearDocument();
        ui::say(feedback_, QStringLiteral("已移除文件。重新选择后即可下单。"));
    });
    connect(pay_, &QPushButton::clicked, this, &StudentWindow::showPayment);
    connect(orders_, &QTableWidget::itemSelectionChanged, this, &StudentWindow::updateActions);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        const int row = orders_->currentRow();
        if (row < 0 || row >= orderIds_.size())
            return;
        const auto id = orderIds_[row];
        const auto job = std::find_if(view_.jobs.cbegin(), view_.jobs.cend(),
                                      [&id](const JobView &item) { return item.id == id; });
        if (job == view_.jobs.cend())
            return;
        // Cancelling stops a print that may already be on paper, so ask once.
        ui::confirm(this, "cancelDialog", QStringLiteral("取消这个订单？"),
                    shortName(job->title) +
                        (job->state == JobState::Running
                             ? QStringLiteral("\n订单正在打印，取消后会停止，已打印的部分作废。")
                             : QStringLiteral("\n订单还在排队，取消后立即退出队列。")) +
                        QStringLiteral("\n%1 将按原路退回（模拟）。").arg(text::money(job->quotedCents)),
                    QStringLiteral("取消订单并退款"), [this, id] {
                        successMessage_ = QStringLiteral("已提交取消；停止打印后按原路退款（模拟）。");
                        emit cancelRequested(id);
                    });
    });

    qRegisterMetaType<ViewState>();
    controller_ = new PrintController(options);
    controller_->moveToThread(&backendThread_);
    connect(&backendThread_, &QThread::started, controller_, &PrintController::initialize);
    connect(&backendThread_, &QThread::finished, controller_, &QObject::deleteLater);
    connect(this, &StudentWindow::quoteRequested, controller_, &PrintController::requestQuote,
            Qt::QueuedConnection);
    connect(this, &StudentWindow::orderRequested, controller_, &PrintController::submitOrder,
            Qt::QueuedConnection);
    connect(this, &StudentWindow::cancelRequested, controller_, &PrintController::cancel,
            Qt::QueuedConnection);
    connect(this, &StudentWindow::closeRequested, controller_, &PrintController::requestClose,
            Qt::QueuedConnection);
    connect(controller_, &PrintController::quoteReady, this, &StudentWindow::applyQuote,
            Qt::QueuedConnection);
    connect(controller_, &PrintController::orderResult, this, &StudentWindow::applyOrderResult,
            Qt::QueuedConnection);
    connect(controller_, &PrintController::viewChanged, this, &StudentWindow::applyView,
            Qt::QueuedConnection);
    connect(
        controller_, &PrintController::feedback, this,
        [this](const QString &message, bool problem) {
            // Accepted orders are worded by applyOrderResult; other confirmations use the pending action's
            // text.
            if (problem)
                ui::say(feedback_, message, "error");
            else if (!message.contains(QStringLiteral("已确认")))
                ui::say(feedback_, message);
            else if (!successMessage_.isEmpty())
                ui::say(feedback_, successMessage_, "ready");
            successMessage_.clear();
        },
        Qt::QueuedConnection);
    connect(
        controller_, &PrintController::closeReady, this,
        [this](bool allowed) {
            closePending_ = false;
            centralWidget()->setEnabled(true);
            if (allowed) {
                closeApproved_ = true;
                close();
            }
        },
        Qt::QueuedConnection);
    officePreview_ = new OfficePreview(this);
    connect(officePreview_, &OfficePreview::ready, this, [this](const QString &pdf) {
        documentLoading_ = false;
        const bool previewed = proof_->loadDocument(pdf);
        if (previewed) {
            pageCount_->setValue(proof_->documentPages());
            file_->setText(QStringLiteral("共 %1 页 · 本地预览").arg(proof_->documentPages()));
            ui::say(feedback_, QStringLiteral("文档已转换并载入，确认打印设置后下单。"), "ready");
        } else {
            file_->setText(QStringLiteral("转换后的文档无法预览"));
            ui::say(feedback_, proof_->previewError(), "error");
        }
        file_->setToolTip(file_->text());
        pageCount_->setEnabled(!previewed);
        updatePrice();
    });
    connect(officePreview_, &OfficePreview::failed, this, [this](const QString &message) {
        documentLoading_ = false;
        proof_->setPreviewMessage(message);
        file_->setText(QStringLiteral("预览失败 · 请更换文件或导出 PDF"));
        file_->setToolTip(message);
        pageCount_->setEnabled(true);
        ui::say(feedback_, message, "error");
        updatePrice();
    });
    clearDocument();
    updatePrice();
    backendThread_.start();
}
StudentWindow::~StudentWindow() {
    officePreview_->cancel();
    backendThread_.quit();
    backendThread_.wait();
}

QString StudentWindow::student() const { return student_->text().trimmed(); }
QJsonObject StudentWindow::order() const {
    const auto title = title_->text().trimmed();
    return {{"title", title.isEmpty() ? QStringLiteral("未命名文档") : title},
            {"pages", pageCount_->value()},
            {"copies", copies_->value()},
            {"paper", paper_->currentIndex() == 1 ? "a3" : "a4"},
            {"color", color_->isChecked()},
            {"duplex", duplex_->isChecked()},
            {"service", QStringList{"express", "standard", "flexible"}[service_->checkedId()]}};
}
void StudentWindow::setDocument(const QString &path) {
    const auto document = inspectDocument(path);
    if (!document.error.isEmpty()) {
        ui::say(feedback_, QStringLiteral("无法使用这个文件：") + document.error, "error");
        return;
    }
    officePreview_->cancel();
    officeRequired_ = OfficePreview::supports(path);
    documentLoading_ = officeRequired_;
    pageCount_->setEnabled(!officeRequired_);
    bool previewed = false;
    if (officeRequired_)
        proof_->setPreviewMessage(QStringLiteral("正在准备文档预览…"));
    else
        previewed = proof_->loadDocument(path);
    documentChosen_ = true;
    title_->setText(document.name);
    if (previewed)
        pageCount_->setValue(proof_->documentPages());
    else if (document.pages > 0)
        pageCount_->setValue(document.pages);
    else
        pageCount_->setValue(1);
    fileName_->setProperty("fullText", document.name);
    fileName_->setText(QFontMetrics(fileName_->font()).elidedText(
        document.name, Qt::ElideMiddle, fileName_->width()));
    fileName_->setToolTip(document.name);
    file_->setText(QStringLiteral("%1 · %2").arg(
        sizeText(document.bytes), documentLoading_ ? QStringLiteral("正在准备预览…") : previewed
                                      ? QStringLiteral("共 %1 页 · 本地预览").arg(proof_->documentPages())
                                      : QStringLiteral("未能识别页数，请在下方填写")));
    file_->setToolTip(file_->text());
    choose_->setText(QStringLiteral("更换文件"));
    remove_->show();
    ui::restyle(drop_, "state", "filled");
    ui::say(feedback_,
            documentLoading_ ? QStringLiteral("正在转换文档，完成后自动显示预览和页数…")
            : previewed ? QStringLiteral("文件已在本地预览，确认设置后下单；模拟支付，不会扣款。")
                      : proof_->previewError(),
            previewed ? "ready" : "waiting");
    pages_->setCurrentIndex(0);
    updatePrice();
    if (officeRequired_) officePreview_->start(path);
}
void StudentWindow::clearDocument() {
    if (officePreview_) officePreview_->cancel();
    officeRequired_ = false;
    documentLoading_ = false;
    pageCount_->setEnabled(true);
    documentChosen_ = false;
    proof_->clearDocument();
    fileName_->setProperty("fullText", QVariant());
    fileName_->setText(QStringLiteral("拖入文件，或点击选择"));
    fileName_->setToolTip({});
    file_->setText(QStringLiteral("PDF / Word / PPT / Excel / 图片 / 文本 · 本地预览"));
    choose_->setText(QStringLiteral("选择文件"));
    remove_->hide();
    ui::restyle(drop_, "state", "");
    title_->clear();
    updateProof();
    updateActions();
}
// The local price is only an instant preview; the backend quote that follows is authoritative.
void StudentWindow::updatePrice() {
    updateProof();
    const bool valid = pageCount_->value() * copies_->value() <= 10000;
    Job job;
    job.pages = pageCount_->value();
    job.copies = copies_->value();
    job.paper = paper_->currentIndex() == 1 ? PaperSize::A3 : PaperSize::A4;
    job.color = color_->isChecked();
    job.duplex = duplex_->isChecked();
    const QStringList tiers{QStringLiteral("快速"), QStringLiteral("普通"), QStringLiteral("灵活")};
    const QStringList windows{QStringLiteral("30 分钟内取件"), QStringLiteral("2 小时内取件"),
                              QStringLiteral("8 小时内取件")};
    const QStringList discounts{QStringLiteral("原价"), QStringLiteral("95 折"), QStringLiteral("85 折")};
    for (int tier = 0; tier < 3; ++tier) {
        job.service = static_cast<ServiceTier>(tier);
        if (valid)
            quote(job);
        service_->button(tier)->setProperty("caption", tiers[tier]);
        service_->button(tier)->setProperty("window", windows[tier]);
        service_->button(tier)->setProperty("price",
                                            valid ? text::money(job.quotedCents) : QStringLiteral("—"));
        service_->button(tier)->setText(tiers[tier] + " " + windows[tier] + " " +
                                        text::money(job.quotedCents));
        service_->button(tier)->update();
    }
    if (!valid) {
        price_->setText(QStringLiteral("—"));
        ui::say(priceDetail_, QStringLiteral("总面数（页数 × 份数）不得超过 10000"), "error");
        quoteAccepted_ = false;
        quotePending_ = false;
        ++quoteToken_;
        updateActions();
        return;
    }
    job.service = static_cast<ServiceTier>(service_->checkedId());
    quote(job);
    quotedCents_ = job.quotedCents;
    price_->setText(text::money(job.quotedCents));
    ui::say(priceDetail_, QStringLiteral("%1 面 · %2 张纸 · 原价 %3 · %4")
                              .arg(job.sheets())
                              .arg(job.paperSheets())
                              .arg(text::money(job.baseCents), discounts[service_->checkedId()]));
    ++quoteToken_; // a reply to the previous options must not overwrite this preview
    quotePending_ = true;
    quoteDelay_->start();
    updateActions();
}
void StudentWindow::updateProof() {
    if (!proof_)
        return;
    summary_->setText(QStringLiteral("%1 页 / %2 / %3 / %4")
                          .arg(pageCount_->value())
                          .arg(paper_->currentText(),
                               color_->isChecked() ? QStringLiteral("彩色") : QStringLiteral("黑白"),
                               duplex_->isChecked() ? QStringLiteral("双面") : QStringLiteral("单面")));
    proof_->setSpecification(title_->text(), pageCount_->value(), copies_->value(),
                             paper_->currentIndex() == 1, color_->isChecked(), duplex_->isChecked(),
                             documentChosen_);
    flipProof_->setEnabled(duplex_->isChecked());
    flipProof_->setText(proof_->showingBack() ? QStringLiteral("▱\n正面") : QStringLiteral("▱\n翻页"));
    flipProof_->setToolTip(duplex_->isChecked() ? QStringLiteral("查看同一张纸上的另一页；双击纸张放大")
                                                : QStringLiteral("选择双面打印后可查看背面"));
}
void StudentWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (orderColumns_)
        orderColumns_->setDirection(width() < 1120 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (settingsDock_)
        settingsDock_->setDirection(width() < 1120 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}
void StudentWindow::applyQuote(int token, const QJsonObject &quote) {
    if (token != quoteToken_)
        return;
    quotePending_ = false;
    quoteAccepted_ = quote.value("type").toString() == QLatin1String("quote");
    if (quoteAccepted_) {
        quotedCents_ = static_cast<Money>(quote.value("quoted_cents").toDouble());
        price_->setText(text::money(quotedCents_));
        deadline_->setText(QStringLiteral("承诺 %1 前可取件。预计时间按当前队列推算，不含之后到达的订单。")
                               .arg(QDateTime::fromMSecsSinceEpoch(
                                        static_cast<qint64>(quote.value("deadline_ms").toDouble()))
                                        .toString("MM-dd HH:mm")));
    } else
        deadline_->clear();
    deadline_->setVisible(quoteAccepted_);
    const QString forecast = text::quoteForecast(quote);
    eta_->setToolTip(forecast);
    if (quoteAccepted_ && quote.value("estimated_finish_ms").isDouble() && !quote.value("paused").toBool()) {
        const auto total = static_cast<qint64>(quote.value("estimated_finish_ms").toDouble() -
                                               quote.value("now_ms").toDouble());
        const auto minutes = std::max<qint64>(1, (total + 59999) / 60000);
        eta_->setText(minutes == 1 ? QStringLiteral("预计 1 分钟内可取件")
                                  : QStringLiteral("预计 %1 分钟后可取件").arg(minutes));
        deadline_->setText(QStringLiteral("前面 %1 单 · 按当前队列估算").arg(quote.value("jobs_ahead").toInt()));
        deadline_->setToolTip(forecast + "\n" + QStringLiteral("承诺取件时间：") +
                             QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(quote.value("deadline_ms").toDouble()))
                                 .toString("MM-dd HH:mm"));
    } else
        eta_->setText(forecast);
    const bool known = quote.value("estimated_finish_ms").isDouble();
    tone(eta_, !quoteAccepted_ ? "error" : !known || quote.value("paused").toBool() ? "waiting" : "ready");
    updateActions();
}
void StudentWindow::updateActions() {
    // The first unmet condition, in the order the student can fix them.
    const QString blocker = !view_.connected ? QStringLiteral("店铺暂时连不上，恢复连接后即可支付。")
                            : !view_.shop.value("shop_enabled").toBool()
                                ? QStringLiteral("这个数据目录不是经营模式，无法下单。")
                            : !documentChosen_                   ? QStringLiteral("先选择要打印的文件。")
                            : documentLoading_                  ? QStringLiteral("正在准备文档预览，完成后即可下单。")
                            : officeRequired_ && proof_->documentPages() == 0
                                ? QStringLiteral("请先完成文档预览，或将文件导出 PDF 后重试。")
                            : title_->text().trimmed().isEmpty() ? QStringLiteral("请填写文档名称。")
                            : student().isEmpty()                ? QStringLiteral("请先在右上角填写学号。")
                            : quoteAccepted_                     ? QString()
                            : quotePending_                      ? QStringLiteral("正在向店铺确认报价…")
                                            : QStringLiteral("当前设置暂时无法下单，原因见上方说明。");
    pay_->setEnabled(blocker.isEmpty());
    pay_->setText(QStringLiteral("确认并下单  →"));
    pay_->setToolTip(blocker);
    payHint_->setText(blocker);
    payHint_->setVisible(!blocker.isEmpty());
    const int row = orders_->currentRow();
    bool active = false;
    if (row >= 0 && row < orderIds_.size())
        for (const auto &job : view_.jobs)
            if (job.id == orderIds_[row])
                active = job.state == JobState::Queued || job.state == JobState::Running;
    cancel_->setEnabled(view_.connected && active);
    cancel_->setToolTip(active ? QStringLiteral("取消选中的订单，已付金额按原路退回（模拟）")
                               : QStringLiteral("选中一个等待中或打印中的订单后可取消"));
}
void StudentWindow::showPayment() {
    if (paymentDialog_)
        return;
    const auto method = static_cast<PaymentMethod>(payment_->checkedId());
    auto *dialog = new EditorialDialog(this, false);
    dialog->setObjectName("paymentDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("确认支付（模拟）"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setFixedWidth(520);
    auto *shell = new QVBoxLayout(dialog);
    shell->setContentsMargins(1, 1, 1, 1);
    shell->setSpacing(0);
    auto *content = new QWidget;
    content->setObjectName("paymentContent");
    content->setStyleSheet("QWidget#paymentContent{background:#FAFAF9;}");
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(32, 22, 32, 26);
    body->setSpacing(10);
    auto *heading = new QLabel(QStringLiteral("确认这次打印"));
    heading->setStyleSheet("color:#14213E;font-size:26px;font-weight:700;");
    auto *headingRow = new QHBoxLayout;
    headingRow->setSpacing(12);
    auto *mark = new QLabel;
    mark->setPixmap(editorialBrandIcon().pixmap(32, 32));
    headingRow->addWidget(mark);
    headingRow->addWidget(heading, 1);
    headingRow->addWidget(new EditorialWindowButton(dialog, EditorialWindowButton::Close, "dialogClose"));
    body->addLayout(headingRow);
    auto *total = new QHBoxLayout;
    total->addWidget(label(QStringLiteral("订单总额"), "muted"));
    total->addStretch();
    auto *amount = label(text::money(quotedCents_), "price");
    amount->setObjectName("paymentAmount");
    total->addWidget(amount);
    body->addLayout(total);
    auto *summary = card("summary");
    auto *rows = new QGridLayout(summary);
    rows->setContentsMargins(0, 14, 0, 14);
    rows->setHorizontalSpacing(14);
    rows->setVerticalSpacing(10);
    rows->setColumnStretch(1, 1);
    const QStringList captions{QStringLiteral("文档"), QStringLiteral("规格"), QStringLiteral("费用"),
                               QStringLiteral("取件")};
    const QStringList values{shortName(title_->text().trimmed()),
                             QStringLiteral("%1 %2 %3 · %4 页 × %5 份")
                                 .arg(paper_->currentText(),
                                      color_->isChecked() ? QStringLiteral("彩色") : QStringLiteral("黑白"),
                                      duplex_->isChecked() ? QStringLiteral("双面") : QStringLiteral("单面"))
                                 .arg(pageCount_->value())
                                 .arg(copies_->value()),
                             priceDetail_->text(), eta_->text()};
    for (int i = 0; i < captions.size(); ++i) {
        rows->addWidget(label(captions[i], "muted"), i, 0, Qt::AlignTop);
        auto *value = label(values[i], "body");
        value->setWordWrap(true);
        value->setStyleSheet("color:#14213E;font-size:14px;");
        value->setMinimumWidth(300);
        if (i == 0) value->setToolTip(title_->text().trimmed());
        rows->addWidget(value, i, 1);
    }
    body->addWidget(summary);
    auto *methodsCaption = new QLabel(QStringLiteral("支付方式"));
    methodsCaption->setStyleSheet("font-size:16px;font-weight:600;color:#14213E;");
    body->addWidget(methodsCaption);
    auto *methods = new QHBoxLayout;
    methods->setSpacing(10);
    auto *methodsGroup = new QButtonGroup(dialog);
    methodsGroup->setExclusive(true);
    for (auto m : {PaymentMethod::Wechat, PaymentMethod::Alipay, PaymentMethod::CampusCard}) {
        auto *b = new PaymentMethodButton(text::payment(m));
        b->setObjectName("paymentMethod_" + paymentName(m));
        b->setIcon(paymentIcon(paymentName(m)));
        b->setIconSize(QSize(24, 24));
        b->setAutoDefault(false);
        b->setMinimumHeight(72);
        b->setCheckable(true);
        b->setChecked(m == method);
        methodsGroup->addButton(b, static_cast<int>(m));
        methods->addWidget(b, 1);
    }
    body->addLayout(methods);
    auto *receipt = new ReceiptCodePanel(paymentCodesDirectory_, true);
    receipt->setMethod(paymentName(method));
    body->addWidget(receipt);
    connect(methodsGroup, &QButtonGroup::idClicked, this,
            [this, receipt](int id) {
                payment_->button(id)->setChecked(true);
                receipt->setMethod(paymentName(static_cast<PaymentMethod>(id)));
            });
    body->addWidget(label(QStringLiteral("应用内确认仅为模拟支付；扫码转账以支付平台实际账单为准。"), "muted"));
    auto *status = label({}, "muted");
    status->setObjectName("paymentStatus");
    status->setWordWrap(true);
    status->setMinimumHeight(22);
    body->addWidget(status);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    auto *back = new QPushButton(QStringLiteral("返回修改"));
    back->setObjectName("cancelPayment");
    back->setAutoDefault(false);
    back->setMinimumHeight(48);
    buttons->addWidget(back, 1);
    auto *confirm = new QPushButton(QStringLiteral("确认支付"));
    confirm->setObjectName("confirmPayment");
    confirm->setProperty("role", "primary");
    confirm->setDefault(true);
    confirm->setMinimumHeight(48);
    buttons->addWidget(confirm, 1);
    body->addLayout(buttons);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("paymentScroll");
    scroll->setStyleSheet("QScrollArea#paymentScroll{background:#FAFAF9;border:none;}");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    shell->addWidget(scroll);
    content->ensurePolished();
    body->activate();
    dialog->resize(520, std::min(content->sizeHint().height() + 24, screen()->availableGeometry().height() - 40));
    connect(back, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, [this, dialog, confirm, back, status] {
        dialog->setProperty("busy", true);
        dialog->findChild<QPushButton *>("dialogClose")->setEnabled(false);
        confirm->setText(QStringLiteral("提交中…"));
        confirm->setEnabled(false);
        back->setEnabled(false);
        dialog->findChild<QFrame *>("paymentReceipt")->setEnabled(false);
        for (const auto *name : {"wechat", "alipay", "campus_card"})
            dialog->findChild<QPushButton *>(QStringLiteral("paymentMethod_") + name)->setEnabled(false);
        ui::say(status, QStringLiteral("支付处理中…"));
        auto request = order();
        request.insert("payment", paymentName(static_cast<PaymentMethod>(payment_->checkedId())));
        request.insert("customer", student());
        emit orderRequested(request);
    });
    paymentDialog_ = dialog;
    dialog->open();
}
void StudentWindow::applyOrderResult(bool accepted, const QString &id, const QString &message) {
    if (!accepted) {
        const auto reason = QStringLiteral("订单未提交，未扣款：") + message;
        ui::say(feedback_, reason, "error");
        if (!paymentDialog_)
            return;
        paymentDialog_->setProperty("busy", false);
        paymentDialog_->findChild<QPushButton *>("dialogClose")->setEnabled(true);
        paymentDialog_->findChild<QPushButton *>("confirmPayment")->setText(QStringLiteral("确认支付"));
        ui::say(paymentDialog_->findChild<QLabel *>("paymentStatus"), reason, "error");
        paymentDialog_->findChild<QPushButton *>("confirmPayment")->setEnabled(true);
        paymentDialog_->findChild<QPushButton *>("cancelPayment")->setEnabled(true);
        paymentDialog_->findChild<QFrame *>("paymentReceipt")->setEnabled(true);
        for (const auto *name : {"wechat", "alipay", "campus_card"})
            paymentDialog_->findChild<QPushButton *>(QStringLiteral("paymentMethod_") + name)->setEnabled(true);
        return;
    }
    if (paymentDialog_)
        paymentDialog_->accept();
    ui::say(feedback_,
            QStringLiteral("支付成功（模拟），订单已进入队列。取件码 %1").arg(text::pickupCode(id)), "ready");
    clearDocument();
    pendingSelect_ = id; // show the student where the order they just paid for is
    pages_->setCurrentIndex(1);
    applyView(view_);
}
void StudentWindow::applyView(const ViewState &view) {
    const bool connectedNow = view.connected && !view_.connected;
    view_ = view;
    if (view.connected)
        emit backendReady();
    const bool shop = view.shop.value("shop_enabled").toBool();
    connection_->setText(!view.connected ? QStringLiteral("店铺暂时连不上")
                         : !shop         ? QStringLiteral("需要经营模式")
                         : view.paused   ? QStringLiteral("店铺暂停派发")
                                         : QStringLiteral("店铺营业中"));
    connection_->setToolTip(view.connection);
    tone(connection_, !view.connected || !shop ? "error" : view.paused ? "waiting" : "ready");
    auto *shopStatus = findChild<QLabel *>("shopStatusDot");
    if (shopStatus) {
        QPalette palette = shopStatus->palette();
        palette.setColor(QPalette::WindowText, !view.connected || !shop ? QColor("#B9524A")
                                              : view.paused ? QColor("#94682C") : QColor("#138342"));
        shopStatus->setPalette(palette);
        shopStatus->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                                     .arg(palette.color(QPalette::WindowText).name()));
        shopStatus->setAccessibleName(connection_->text());
    }
    if (view.connected && !shop)
        ui::say(feedback_, QStringLiteral("这个数据目录是旧版同构队列；学生端需要经营模式的数据目录。"),
                "error");
    if (connectedNow)
        quoteDelay_->start();

    const auto me = student();
    const auto selected = orders_->currentRow() >= 0 && orders_->currentRow() < orderIds_.size()
                              ? orderIds_[orders_->currentRow()]
                              : QString();
    std::vector<const JobView *> mine, waiting;
    for (auto job = view_.jobs.crbegin(); job != view_.jobs.crend(); ++job)
        if (!me.isEmpty() && job->customer == me)
            mine.push_back(&*job);
    for (const auto &job : view_.jobs)
        if (job.state == JobState::Queued)
            waiting.push_back(&job);
    int active = 0, ready = 0;
    const JobView *soonest = nullptr;
    orderIds_.clear();
    {
        const QSignalBlocker blocker(orders_);
        orders_->setRowCount(static_cast<int>(mine.size()));
        for (int row = 0; row < static_cast<int>(mine.size()); ++row) {
            const auto &job = *mine[static_cast<std::size_t>(row)];
            orderIds_ << job.id;
            const bool live = job.state == JobState::Queued || job.state == JobState::Running ||
                              job.state == JobState::Cancelling;
            if (live)
                ++active;
            if (job.state == JobState::Completed)
                ++ready;
            if (live && job.estimatedFinishMs >= 0 &&
                (!soonest || job.estimatedFinishMs < soonest->estimatedFinishMs))
                soonest = &job;
            const bool refunded = job.state == JobState::Cancelled || job.state == JobState::Failed;
            const auto paid = refunded ? QStringLiteral("已原路退款")
                              : job.refundCents > 0
                                  ? QStringLiteral("已退款 %1").arg(text::money(job.refundCents))
                                  : text::payment(job.payment) + QStringLiteral("已支付");
            QString progress = text::forecast(job, text::forecastNow(view_), view_.paused);
            if (job.state == JobState::Queued && job.estimatedFinishMs >= 0 && !view_.paused)
                progress = QStringLiteral("第 %1 位 · %2 · 预计 %3 完成")
                               .arg(job.ahead + 1)
                               .arg(text::startsIn(job.estimatedStartMs - view_.nowMs),
                                    text::clock(job.estimatedFinishMs));
            else if (job.state == JobState::Running && job.estimatedFinishMs >= 0)
                progress +=
                    QStringLiteral("（还需%1）").arg(text::duration(job.estimatedFinishMs - view_.nowMs));
            else if (job.state == JobState::Completed)
                progress = QStringLiteral("已完成，可凭取件码取件");
            else if (job.state == JobState::Cancelling)
                progress = QStringLiteral("正在停止打印");
            else if (job.state == JobState::Failed)
                progress = QStringLiteral("打印失败");
            else if (job.state == JobState::Cancelled)
                progress = QStringLiteral("已取消");
            else if (progress.isEmpty())
                progress = QStringLiteral("暂无法预计");
            const bool done = job.state == JobState::Completed;
            const QList<ui::Cell> cells{
                {job.title, "", -1, "strong"},
                job.specification + QStringLiteral(" · %1 面").arg(job.sheets),
                text::money(job.quotedCents) + QStringLiteral(" · ") + paid,
                {text::state(job.state), text::tone(job.state)},
                progress,
                {QStringLiteral("%1 / %2 面").arg(job.printed).arg(job.sheets), "",
                 job.sheets > 0 ? double(job.printed) / job.sheets : 0.},
                {done ? text::pickupCode(job.id) : QStringLiteral("—"), "", -1, done ? "accent" : "muted"}};
            for (int column = 0; column < cells.size(); ++column)
                ui::setCell(orders_, row, column, cells[column]);
        }
        int row = pendingSelect_.isEmpty() ? -1 : static_cast<int>(orderIds_.indexOf(pendingSelect_));
        if (row >= 0)
            pendingSelect_.clear();
        else
            row = static_cast<int>(orderIds_.indexOf(selected));
        if (row != orders_->currentRow())
            orders_->setCurrentCell(row, 0);
    }
    ordersEmpty_->setVisible(mine.empty());
    ordersSummary_->setText(me.isEmpty() ? QStringLiteral("填写学号后显示订单")
                                         : QStringLiteral("学号 %1 · 共 %2 单").arg(me).arg(mine.size()));

    const auto fleet = view_.shop.value("fleet").toArray();
    int printing = 0;
    if (devices_->rowCount() != static_cast<int>(view_.printers.size())) {
        devices_->setRowCount(static_cast<int>(view_.printers.size()));
        ui::fitRows(devices_);
    }
    for (int i = 0; i < static_cast<int>(view_.printers.size()); ++i) {
        const auto object = fleet[i].toObject();
        const auto &printer = view_.printers[static_cast<std::size_t>(i)];
        const bool busy = !printer.jobId.isEmpty(), online = object.value("online").toBool(true);
        if (busy)
            ++printing;
        const QString status =
            (busy ? QStringLiteral("打印中 %1%")
                            .arg(printer.total > 0 ? printer.printed * 100 / printer.total : 0) +
                        (!me.isEmpty() && printer.customer == me ? QStringLiteral(" · 我的订单") : QString())
                  : QStringLiteral("空闲")) +
            (online ? QString() : QStringLiteral(" · 暂停接新单"));
        const bool timed = busy && printer.estimatedFinishMs >= 0;
        const QList<ui::Cell> cells{{text::deviceName(object, i), "", -1, "strong"},
                                    text::capability(object),
                                    {status, !online ? "waiting"
                                             : busy  ? "info"
                                                     : "ready"},
                                    {timed ? text::clock(printer.estimatedFinishMs) : QStringLiteral("—"), "",
                                     -1, timed ? "" : "muted"}};
        for (int column = 0; column < cells.size(); ++column)
            ui::setCell(devices_, i, column, cells[column]);
    }
    // Forecast order first; orders that cannot be estimated go last.
    std::stable_sort(waiting.begin(), waiting.end(), [](const JobView *a, const JobView *b) {
        const bool knownA = a->estimatedFinishMs >= 0, knownB = b->estimatedFinishMs >= 0;
        return knownA != knownB ? knownA : knownA && a->ahead < b->ahead;
    });
    const int shown = std::min(50, static_cast<int>(waiting.size()));
    queue_->setRowCount(shown);
    queueEmpty_->setVisible(shown == 0);
    for (int row = 0; row < shown; ++row) {
        const auto &job = *waiting[static_cast<std::size_t>(row)];
        const bool known = job.estimatedFinishMs >= 0, own = !me.isEmpty() && job.customer == me;
        const QList<ui::Cell> cells{
            {QString::number(row + 1), "", -1, "muted"},
            {own ? QStringLiteral("我的订单 · ") + job.title : QStringLiteral("其他订单"), "", -1,
             own ? "accent" : ""},
            job.specification + QStringLiteral(" · %1 面").arg(job.sheets),
            !known ? text::blocked(job.blocked) : view_.paused ? text::afterResume(job.estimatedStartMs - text::forecastNow(view_))
                : text::clock(job.estimatedStartMs),
            known && !view_.paused ? text::clock(job.estimatedFinishMs) : QStringLiteral("—")};
        for (int column = 0; column < cells.size(); ++column)
            ui::setCell(queue_, row, column, cells[column]);
    }

    metricValues_[0]->setText(QStringLiteral("%1 单").arg(waiting.size()));
    metricHints_[0]->setText(QStringLiteral("%1 / %2 台设备正在打印").arg(printing).arg(view_.printers.size()));
    metricValues_[1]->setText(view_.queueClearMs < 0 ? (waiting.empty() && printing == 0 ? QStringLiteral("无需等待") : QStringLiteral("暂无法预计"))
        : text::duration(view_.queueClearMs - text::forecastNow(view_)));
    metricHints_[1]->setText(view_.paused ? QStringLiteral("店铺暂停派发，恢复后开始计时") : QStringLiteral("按当前队列推算，不含新订单"));
    metricValues_[2]->setText(QStringLiteral("%1 单").arg(active));
    metricHints_[2]->setText(
        !soonest       ? QStringLiteral("暂无可预计的进行中订单")
        : view_.paused ? QStringLiteral("店铺暂停派发，恢复后继续")
                       : QStringLiteral("最近一单预计 %1 完成").arg(text::clock(soonest->estimatedFinishMs)));
    metricValues_[3]->setText(QStringLiteral("%1 单").arg(ready));
    metricHints_[3]->setText(QStringLiteral("凭“我的订单”里的取件码到店取件"));
    tone(metricValues_[3], ready > 0 ? "ready" : "");
    updateActions();
}
void StudentWindow::dragEnterEvent(QDragEnterEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile())
        return;
    pages_->setCurrentIndex(0);
    ui::restyle(drop_, "state", "active");
    event->acceptProposedAction();
}
void StudentWindow::dragLeaveEvent(QDragLeaveEvent *) {
    ui::restyle(drop_, "state", documentChosen_ ? "filled" : "");
}
void StudentWindow::dropEvent(QDropEvent *event) {
    ui::restyle(drop_, "state", documentChosen_ ? "filled" : "");
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile())
        return;
    setDocument(urls.first().toLocalFile());
    event->acceptProposedAction();
}
void StudentWindow::closeEvent(QCloseEvent *event) {
    if (closeApproved_) {
        event->accept();
        return;
    }
    event->ignore();
    if (closePending_)
        return;
    closePending_ = true;
    centralWidget()->setEnabled(false);
    emit closeRequested();
}
} // namespace cps::gui
