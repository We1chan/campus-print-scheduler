#pragma once

#include <QString>

namespace cps {
// Call before QCoreApplication, QProcess, threads, or any Qt event loop exists.
// Re-exec the process after detachment: macOS frameworks cannot safely continue
// using their inherited state after fork. Arguments must omit --daemon.
// Returns false on an early failure; otherwise this process exits or execs.
bool detachDaemon(const char* executable, char* const arguments[], QString& error);
} // namespace cps
