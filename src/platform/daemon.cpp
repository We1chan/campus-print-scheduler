#include "cps/platform/daemon.hpp"

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cps {
bool detachDaemon(const char* executable, char* const arguments[], QString& error) {
#ifdef Q_OS_UNIX
    const long maxFd = sysconf(_SC_OPEN_MAX);
    const long limit = maxFd > 0 ? maxFd : 1024;
    pid_t pid = fork();
    if (pid < 0) { error = QString::fromLocal8Bit(std::strerror(errno)); return false; }
    if (pid > 0) _exit(0);
    if (setsid() < 0) _exit(71);
    pid = fork();
    if (pid < 0) _exit(71);
    if (pid > 0) _exit(0);
    umask(0027);
    if (chdir("/") != 0) _exit(71);
    for (long fd = 0; fd < limit; ++fd) close(static_cast<int>(fd));
    const int nullFd = open("/dev/null", O_RDWR);
    if (nullFd < 0) _exit(71);
    if (dup2(nullFd, STDIN_FILENO) < 0 || dup2(nullFd, STDOUT_FILENO) < 0 ||
        dup2(nullFd, STDERR_FILENO) < 0) _exit(71);
    if (nullFd > STDERR_FILENO) close(nullFd);
    execv(executable, arguments);
    _exit(71);
#else
    (void)executable;
    (void)arguments;
    error = QStringLiteral("--daemon is POSIX-only; use foreground supervision on Windows.");
    return false;
#endif
}
} // namespace cps
