#pragma once

namespace core {

// Lets the short-lived `featherrpc` CLI tool reach a long-running app
// instance without a full IPC protocol - just three verbs, each mapped to
// whatever the platform's cheapest "wake a process up" primitive is
// (SIGHUP/SIGTERM on POSIX, named Event objects on Windows).
class DaemonSignal {
public:
    virtual ~DaemonSignal() = default;

    virtual bool IsRunning() const = 0;

    // Triggers core::LoadConfig() + PresenceEngine::UpdateConfig() in the
    // running instance - the same call path the tray's own config-changed
    // callback already uses. Returns false if nothing is running.
    virtual bool RequestReload() = 0;

    // Triggers a clean PresenceEngine::Stop() + process exit. Returns
    // false if nothing is running.
    virtual bool RequestQuit() = 0;
};

} // namespace core
