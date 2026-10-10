#pragma once
#include <QString>

namespace cps {
// Expand Windows DOS 8.3 aliases before a canonical path is used as an identity.
// Other platforms, and paths the OS cannot expand, keep their existing spelling.
QString expandPathAliases(const QString& path);
}
