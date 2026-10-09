#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <io.h>
#include <cstdio>
#include <cstdlib>
#endif

// QtTest opens report files through the CRT. MSVC makes these handles
// inheritable; a detached coordinator must not keep a test report locked.
QString isolateTestReportHandles() {
#ifdef Q_OS_WIN
#ifdef _MSC_VER
    struct RestoreInvalidParameterHandler {
        _invalid_parameter_handler previous = _set_thread_local_invalid_parameter_handler(
            [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {});
        ~RestoreInvalidParameterHandler() { _set_thread_local_invalid_parameter_handler(previous); }
    } restore;
#endif
    const auto arguments = QCoreApplication::arguments();
    QStringList reports;
    for (int i = 1; i + 1 < arguments.size(); ++i) {
        if (arguments[i] != "-o") continue;
        const auto filename = arguments[++i].section(',', 0, -2);
        if (!filename.isEmpty() && filename != "-")
            reports.append(QDir::cleanPath(QFileInfo(filename).absoluteFilePath()));
    }
    for (int fd = 3; fd < _getmaxstdio(); ++fd) {
        const auto raw = _get_osfhandle(fd);
        if (raw < 0) continue;
        const auto handle = reinterpret_cast<HANDLE>(raw);
        wchar_t path[32768];
        const auto length = GetFinalPathNameByHandleW(handle, path, 32768, FILE_NAME_NORMALIZED);
        if (!length || length >= 32768) continue;
        auto filename = QDir::fromNativeSeparators(QString::fromWCharArray(path, int(length)));
        if (filename.startsWith("//?/")) filename.remove(0, 4);
        for (int index = 0; index < reports.size(); ++index) {
            if (filename.compare(reports[index], Qt::CaseInsensitive) != 0) continue;
            DWORD flags = 0;
            if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0)
                || !GetHandleInformation(handle, &flags) || (flags & HANDLE_FLAG_INHERIT))
                return "Cannot prevent QtTest report handle inheritance";
            reports.removeAt(index);
            break;
        }
    }
    if (!reports.isEmpty()) return "QtTest report handle was not found: " + reports.join(", ");
#endif
    return {};
}
