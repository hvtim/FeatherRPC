#pragma once

#include "core/DaemonSignal.h"

namespace platform_posix {

// CLI-side: finds the running app's pid via the pidfile and signals it.
// Shared between Linux and macOS since both are plain POSIX signals.
class PosixDaemonSignal : public core::DaemonSignal {
public:
    bool IsRunning() const override;
    bool RequestReload() override;
    bool RequestQuit() override;
};

// Mode-agnostic: writes the pidfile only, no signal blocking. Safe to call
// from tray mode too - unlike the headless path below, tray mode must keep
// signals deliverable normally so GLib's g_unix_signal_add (see SniTray.cpp)
// can pick up SIGHUP/SIGTERM/SIGINT itself; blocking them here would starve
// that mechanism. This is what makes the CLI's `IsRunning`/`RequestReload`/
// `RequestQuit` work identically regardless of which mode the target
// process is actually running in - same pidfile, same path, written
// unconditionally on startup in either mode.
void DaemonWritePidFile();

// Daemon-side (the long-running app itself), used only when running
// headless (see main_*_daemon paths). Must be called before
// PresenceEngine::Start() - blocks SIGHUP/SIGTERM/SIGINT so they're never
// delivered asynchronously to arbitrary code on another thread (running a
// signal handler mid-allocation/mid-mutex-lock is undefined behavior);
// the blocked mask is inherited by every thread spawned afterward,
// including PresenceEngine's worker. Also writes the pidfile (via
// DaemonWritePidFile() above).
void DaemonBlockSignalsAndWritePidFile();

enum class DaemonSignalKind { Reload, Quit };

// Blocking sigwait() on the calling thread (call this from main(), not a
// background thread - it's meant to BE the app's idle/wait loop while
// headless, replacing the tray's UI message pump).
DaemonSignalKind DaemonWaitForSignal();

// Call once on clean shutdown (after handling a Quit signal). Only
// removes the pidfile if it still holds this process's own pid - a second
// instance that got as far as overwriting it with its own pid (see
// SingleInstanceLock below for why that should no longer happen at all)
// must not have its identity erased by the first instance's shutdown.
void DaemonRemovePidFile();

// Guards against two FeatherRPC processes running at once - tray+tray,
// headless+headless, or tray+headless in any combination - regardless of
// what launched the second one (a manual run, systemd/LaunchAgent
// autostart, or `featherrpc daemon start`). Deliberately a separate lock
// file from the pidfile above rather than reusing it: this lock only ever
// answers "is another instance already alive", it never feeds
// RequestReload()/RequestQuit(), so acquiring it in tray mode can't cause
// a CLI command to send a real SIGHUP/SIGTERM into a tray process that
// isn't set up to handle it as anything other than "terminate" (true
// today on macOS; SniTray.cpp handles it properly on Linux, see
// g_unix_signal_add there, but this lock doesn't depend on that).
//
// Call TryAcquire() once, first thing in main(), before doing anything
// else - keep the returned object alive for the process's entire
// lifetime. The OS releases the underlying flock() automatically on exit
// or crash, so a killed process can never wedge a future launch.
class SingleInstanceLock {
public:
    static SingleInstanceLock TryAcquire();

    SingleInstanceLock(SingleInstanceLock&&) noexcept;
    SingleInstanceLock& operator=(SingleInstanceLock&&) noexcept;
    ~SingleInstanceLock();

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    bool Acquired() const { return fd_ >= 0; }

private:
    explicit SingleInstanceLock(int fd) : fd_(fd) {}
    int fd_ = -1;
};

} // namespace platform_posix
