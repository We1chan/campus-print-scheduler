#pragma once
#include <QProcess>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace cps {
// Keep console tools usable from a terminal, but hide windows for child jobs.
inline void hideProcessConsole(QProcess &process) {
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags = (args->flags & ~CREATE_NEW_CONSOLE) | CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#else
    Q_UNUSED(process);
#endif
}
} // namespace cps
