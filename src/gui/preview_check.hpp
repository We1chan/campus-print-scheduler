#pragma once
#include <QString>
namespace cps::gui {
// Deployment diagnostic: no window/coordinator is started; render real pages.
int checkDocumentPreview(const QString &path, const QString &report);
}
