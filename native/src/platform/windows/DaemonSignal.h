#pragma once

#include "core/DaemonSignal.h"

#include <windows.h>

namespace platform_windows {

// CLI-side: no pidfile needed on Windows - a named Event's existence is
// exactly tied to whether the creating (daemon) process still holds it,
// so OpenEventW succeeding already is the liveness check.
class WindowsDaemonSignal : public core::DaemonSignal {
public:
    bool IsRunning() const override;
    bool RequestReload() override;
    bool RequestQuit() override;
};

enum class DaemonSignalKind { Reload, Quit };

// Daemon-side (headless run path only) - creates the two named Events and
// waits on them in place of the tray's message pump.
class DaemonWaiter {
public:
    DaemonWaiter();
    ~DaemonWaiter();
    DaemonWaiter(const DaemonWaiter&) = delete;
    DaemonWaiter& operator=(const DaemonWaiter&) = delete;

    DaemonSignalKind Wait();

private:
    HANDLE _reloadEvent = nullptr;
    HANDLE _quitEvent = nullptr;
};

} // namespace platform_windows
