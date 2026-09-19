#pragma once

namespace cps {
// IPC uses UTF-8 bytes and exactly one LF delimiter on every operating system.
void configureIpcStdio();
}
