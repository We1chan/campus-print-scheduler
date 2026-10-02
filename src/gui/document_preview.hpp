#pragma once
#include <QByteArray>
#include <QCache>
#include <QImage>
#include <QTextDocument>
#include <memory>

namespace cps::gui {
// Document contents stay in this GUI process. Nothing is sent to the shop IPC.
class DocumentPreview {
  public:
    DocumentPreview();
    ~DocumentPreview();
    DocumentPreview(const DocumentPreview &) = delete;
    DocumentPreview &operator=(const DocumentPreview &) = delete;
    bool load(const QString &path);
    void clear();
    int pageCount() const { return pages_; }
    QString error() const { return error_; }
    bool loaded() const { return pages_ > 0; }
    QImage page(int index);

  private:
    QByteArray bytes_;
    void *pdf_ = nullptr;
    QImage image_;
    std::unique_ptr<QTextDocument> text_;
    QCache<int, QImage> cache_{12};
    QString error_;
    int pages_ = 0;
};
} // namespace cps::gui
