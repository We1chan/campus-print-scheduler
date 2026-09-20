#include "cps/platform/stdio.hpp"
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

namespace cps {
void configureIpcStdio() {
#ifdef Q_OS_WIN
    // CRT text mode would turn an already forwarded CRLF into CRCRLF.
    // Keep the worker -> supervisor -> client pipe byte-preserving.
    (void)_setmode(_fileno(stdout), _O_BINARY);
#else
    // Losing the GUI's stdout pipe must not kill the supervisor before its
    // heartbeat lease has stopped the child process.
    std::signal(SIGPIPE, SIG_IGN);
#endif
}
}
