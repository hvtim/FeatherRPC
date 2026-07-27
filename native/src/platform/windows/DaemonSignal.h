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

// Guards against two FeatherRPC processes running at once - tray+tray,
// headless+headless, or tray+headless in any combination - regardless of
// what launched the second one (a manual run, the Startup shortcut, or
// `featherrpc-cli daemon start`). A separate named mutex from the Reload/
// Quit events above, deliberately: this lock only ever answers "is
// another instance already alive", it's never wired to RequestReload()/
// RequestQuit(), so holding it in tray mode can't make a CLI command try
// to signal a tray process that has no DaemonWaiter listening for it.
//
// Call TryAcquire() once, first thing in wWinMain, before doing anything
// else - keep the returned object alive for the process's entire
// lifetime. Windows releases the mutex automatically when this process's
// last handle to it closes (including on a crash), so a killed process
// can never wedge a future launch.
class SingleInstanceLock {
public:
    static SingleInstanceLock TryAcquire();

    SingleInstanceLock(SingleInstanceLock&&) noexcept;
    SingleInstanceLock& operator=(SingleInstanceLock&&) noexcept;
    ~SingleInstanceLock();

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    bool Acquired() const { return handle_ != nullptr; }

private:
    explicit SingleInstanceLock(HANDLE handle) : handle_(handle) {}
    HANDLE handle_ = nullptr;
};

} // namespace platform_windows
