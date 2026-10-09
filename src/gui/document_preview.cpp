#include "document_preview.hpp"
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLibrary>
#include <QPainter>
#include <cmath>
#include <algorithm>

namespace cps::gui {
namespace {
// PDFium's public C ABI. All calls are confined to the GUI thread. Keeping the
// document byte buffer alive is required by FPDF_LoadMemDocument64.
class Pdfium {
  public:
    QLibrary library;
    void (*init)() = nullptr;
    void (*destroy)() = nullptr;
    void *(*load)(const void *, size_t, const char *) = nullptr;
    void (*close)(void *) = nullptr;
    int (*count)(void *) = nullptr;
    void *(*loadPage)(void *, int) = nullptr;
    void (*closePage)(void *) = nullptr;
    float (*width)(void *) = nullptr;
    float (*height)(void *) = nullptr;
    void *(*bitmap)(int, int, int, void *, int) = nullptr;
    void (*freeBitmap)(void *) = nullptr;
    void (*render)(void *, void *, int, int, int, int, int, int) = nullptr;
    unsigned long (*lastError)() = nullptr;
    bool ready = false;
    Pdfium() {
        library.setFileName(QDir(QCoreApplication::applicationDirPath()).filePath("pdfium"));
        if (!library.load()) {
            library.setFileName("pdfium");
            if (!library.load())
                return;
        }
#define CPS_PDF_BIND(member, symbol)                                                                         \
    member = reinterpret_cast<decltype(member)>(library.resolve(symbol));                                    \
    if (!member)                                                                                             \
    return
        CPS_PDF_BIND(init, "FPDF_InitLibrary");
        CPS_PDF_BIND(destroy, "FPDF_DestroyLibrary");
        CPS_PDF_BIND(load, "FPDF_LoadMemDocument64");
        CPS_PDF_BIND(close, "FPDF_CloseDocument");
        CPS_PDF_BIND(count, "FPDF_GetPageCount");
        CPS_PDF_BIND(loadPage, "FPDF_LoadPage");
        CPS_PDF_BIND(closePage, "FPDF_ClosePage");
        CPS_PDF_BIND(width, "FPDF_GetPageWidthF");
        CPS_PDF_BIND(height, "FPDF_GetPageHeightF");
        CPS_PDF_BIND(bitmap, "FPDFBitmap_CreateEx");
        CPS_PDF_BIND(freeBitmap, "FPDFBitmap_Destroy");
        CPS_PDF_BIND(render, "FPDF_RenderPageBitmap");
        CPS_PDF_BIND(lastError, "FPDF_GetLastError");
#undef CPS_PDF_BIND
        init();
        ready = true;
    }
    ~Pdfium() {
        if (ready)
            destroy();
    }
};
Pdfium &engine() {
    static Pdfium instance;
    return instance;
}
} // namespace
DocumentPreview::DocumentPreview() = default;
DocumentPreview::~DocumentPreview() { clear(); }
void DocumentPreview::clear() {
    if (pdf_)
        engine().close(pdf_);
    pdf_ = nullptr;
    bytes_.clear();
    image_ = {};
    text_.reset();
    cache_.clear();
    pageSizes_.clear();
    pages_ = 0;
    error_.clear();
}
bool DocumentPreview::load(const QString &path) {
    clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 64 * 1024 * 1024) {
        error_ = QStringLiteral("文件无法读取或超过 64 MB");
        return false;
    }
    bytes_ = file.readAll();
    if (bytes_.startsWith("%PDF")) {
        auto &e = engine();
        if (!e.ready) {
            error_ = QStringLiteral("PDF 预览组件缺失，请安装 PDFium 后重启");
            return false;
        }
        pdf_ = e.load(bytes_.constData(), static_cast<size_t>(bytes_.size()), nullptr);
        if (!pdf_) {
            error_ = e.lastError() == 4 ? QStringLiteral("此 PDF 受密码保护，请先解锁")
                                        : QStringLiteral("此 PDF 无法解析，请检查文件是否完整");
            return false;
        }
        pages_ = e.count(pdf_);
        if (pages_ < 1 || pages_ > 1000) {
            clear();
            error_ = QStringLiteral("PDF 页数须在 1–1000 页之间");
            return false;
        }
        return true;
    }
    QImageReader reader(path);
    if (reader.canRead()) {
        const QSize size = reader.size();
        if (size.isEmpty() || qint64(size.width()) * size.height() > 80000000) {
            error_ = QStringLiteral("图片尺寸过大，无法预览");
            return false;
        }
        reader.setAutoTransform(true);
        reader.setScaledSize(size.scaled(1400, 1800, Qt::KeepAspectRatio));
        image_ = reader.read();
        if (!image_.isNull()) {
            pages_ = 1;
            return true;
        }
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (QStringList{"txt", "md", "csv", "log", "c", "h", "cpp", "hpp", "py", "java", "json", "tex"}.contains(
            suffix)) {
        text_ = std::make_unique<QTextDocument>();
        QFont font("Microsoft YaHei");
        font.setPointSizeF(11);
        text_->setDefaultFont(font);
        text_->setDocumentMargin(42);
        text_->setPageSize({595, 842});
        text_->setPlainText(QString::fromUtf8(bytes_));
        pages_ = text_->pageCount();
        if(pages_>1000){text_.reset();pages_=0;error_=QStringLiteral("文本超过 1000 页，请分成多个文件后预览");return false;}
        return true;
    }
    error_ = QStringLiteral("此格式无法直接预览，请先导出为 PDF、图片或 UTF-8 文本");
    return false;
}
QSizeF DocumentPreview::pageSize(int index) {
    if (index < 0 || index >= pages_) return {};
    if (pageSizes_.contains(index)) return pageSizes_.value(index);
    QSizeF size;
    if (pdf_) {
        auto &e = engine();
        if (void *page = e.loadPage(pdf_, index)) {
            const double w = e.width(page), h = e.height(page);
            if (std::isfinite(w) && std::isfinite(h) && w > 0 && h > 0 && w <= 200000 && h <= 200000)
                size = {w, h};
            e.closePage(page);
        }
    } else if (text_) size = text_->pageSize();
    else size = image_.size();
    pageSizes_.insert(index, size);
    return size;
}
QImage DocumentPreview::page(int index) {
    if (index < 0 || index >= pages_)
        return {};
    if (auto *cached = cache_.object(index))
        return *cached;
    QImage result;
    if (pdf_) {
        auto &e = engine();
        void *page = e.loadPage(pdf_, index);
        if (!page)
            return {};
        const double w = e.width(page), h = e.height(page);
        if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0 || w > 200000 || h > 200000) {
            e.closePage(page);
            return {};
        }
        QSize size = QSizeF(w, h).toSize().scaled(1000, 1400, Qt::KeepAspectRatio);
        if (size.isEmpty()) {
            e.closePage(page);
            return {};
        }
        result = QImage(size, QImage::Format_ARGB32);
        result.fill(Qt::white);
        void *bitmap = e.bitmap(size.width(), size.height(), 4, result.bits(), result.bytesPerLine());
        if (bitmap) {
            e.render(bitmap, page, 0, 0, size.width(), size.height(), 0, 1);
            e.freeBitmap(bitmap);
        } else
            result = {};
        e.closePage(page);
    } else if (text_) {
        result = QImage(1000, 1415, QImage::Format_ARGB32);
        result.fill(Qt::white);
        QPainter painter(&result);
        painter.scale(1000. / 595., 1415. / 842.);
        painter.translate(0, -index * 842.);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(0, index * 842., 595, 842);
        text_->documentLayout()->draw(&painter, context);
    } else
        result = image_;
    if (!result.isNull())
        cache_.insert(index, new QImage(result));
    return result;
}
} // namespace cps::gui
