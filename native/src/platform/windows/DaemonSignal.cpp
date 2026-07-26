#include "DaemonSignal.h"

namespace platform_windows {

namespace {
constexpr wchar_t kReloadEventName[] = L"Local\\FeatherRPC-Daemon-Reload";
constexpr wchar_t kQuitEventName[] = L"Local\\FeatherRPC-Daemon-Quit";
} // namespace

bool WindowsDaemonSignal::IsRunning() const {
    HANDLE h = OpenEventW(SYNCHRONIZE, FALSE, kReloadEventName);
    if (!h) {
        return false;
    }
    CloseHandle(h);
    return true;
}

bool WindowsDaemonSignal::RequestReload() {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, kReloadEventName);
    if (!h) {
        return false;
    }
    bool ok = SetEvent(h) != 0;
    CloseHandle(h);
    return ok;
}

bool WindowsDaemonSignal::RequestQuit() {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, kQuitEventName);
    if (!h) {
        return false;
    }
    bool ok = SetEvent(h) != 0;
    CloseHandle(h);
    return ok;
}

DaemonWaiter::DaemonWaiter() {
    // Auto-reset (manualReset=FALSE): each SetEvent wakes exactly one
    // Wait() call and resets itself, so a reload event can fire again on
    // the next call without an explicit ResetEvent.
    _reloadEvent = CreateEventW(nullptr, FALSE, FALSE, kReloadEventName);
    _quitEvent = CreateEventW(nullptr, FALSE, FALSE, kQuitEventName);
}

DaemonWaiter::~DaemonWaiter() {
    if (_reloadEvent) {
        CloseHandle(_reloadEvent);
    }
    if (_quitEvent) {
        CloseHandle(_quitEvent);
    }
}

DaemonSignalKind DaemonWaiter::Wait() {
    HANDLE handles[2] = {_reloadEvent, _quitEvent};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    return (result == WAIT_OBJECT_0) ? DaemonSignalKind::Reload : DaemonSignalKind::Quit;
}

} // namespace platform_windows
