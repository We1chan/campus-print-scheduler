#include "cps/platform/path.hpp"
#include <QDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <vector>
#endif

namespace cps {
QString expandPathAliases(const QString& path) {
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD required = GetLongPathNameW(native.c_str(), nullptr, 0);
    if (required != 0) {
        std::vector<wchar_t> buffer(required);
        const DWORD length = GetLongPathNameW(native.c_str(), buffer.data(), required);
        if (length != 0 && length < required)
            return QDir::fromNativeSeparators(QString::fromWCharArray(buffer.data(), static_cast<int>(length)));
    }
#endif
    return path;
}
}
