#include "cps/platform/stdio.hpp"
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace cps {
void configureIpcStdio() {
#ifdef Q_OS_WIN
    // CRT text mode would turn an already forwarded CRLF into CRCRLF.
    // Keep the worker -> supervisor -> client pipe byte-preserving.
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
}
}
