#include "preview_check.hpp"
#include "document_preview.hpp"
#include "office_preview.hpp"
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace cps::gui {
int checkDocumentPreview(const QString &path, const QString &report) {
    DocumentPreview document;
    OfficePreview office;
    QString error;
    if (OfficePreview::supports(path)) {
        QEventLoop loop;
        QObject::connect(&office, &OfficePreview::ready, &loop, [&](const QString &pdf) {
            if (!document.load(pdf)) error = document.error();
            loop.quit();
        });
        QObject::connect(&office, &OfficePreview::failed, &loop, [&](const QString &message) {
            error = message; loop.quit();
        });
        office.start(path);
        loop.exec();
    } else if (!document.load(path)) error = document.error();
    const auto first = document.page(0), last = document.page(document.pageCount() - 1);
    if (error.isEmpty() && (first.isNull() || last.isNull())) error = QStringLiteral("文档页面无法渲染");
    const auto size = document.pageSize(0);
    const QJsonObject data{{"ok", error.isEmpty()}, {"error", error}, {"pages", document.pageCount()},
        {"width", size.width()}, {"height", size.height()},
        {"first_rendered", !first.isNull()}, {"last_rendered", !last.isNull()}};
    QSaveFile output(report);
    const auto bytes = QJsonDocument(data).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return 74;
    return error.isEmpty() ? 0 : 65;
}
}
