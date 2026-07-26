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

// Daemon-side (the long-running app itself), used only when running
// headless (see main_*_daemon paths). Must be called before
// PresenceEngine::Start() - blocks SIGHUP/SIGTERM/SIGINT so they're never
// delivered asynchronously to arbitrary code on another thread (running a
// signal handler mid-allocation/mid-mutex-lock is undefined behavior);
// the blocked mask is inherited by every thread spawned afterward,
// including PresenceEngine's worker. Also writes the pidfile.
void DaemonBlockSignalsAndWritePidFile();

enum class DaemonSignalKind { Reload, Quit };

// Blocking sigwait() on the calling thread (call this from main(), not a
// background thread - it's meant to BE the app's idle/wait loop while
// headless, replacing the tray's UI message pump).
DaemonSignalKind DaemonWaitForSignal();

// Call once on clean shutdown (after handling a Quit signal).
void DaemonRemovePidFile();

} // namespace platform_posix
