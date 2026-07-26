#include "DaemonSignal.h"

namespace platform_windows {

namespace {
constexpr wchar_t kReloadEventName[] = L"Local\\FeatherRPC-Daemon-Reload";
constexpr wchar_t kQuitEventName[] = L"Local\\FeatherRPC-Daemon-Quit";
constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\FeatherRPC-SingleInstance";
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

SingleInstanceLock SingleInstanceLock::TryAcquire() {
    // bInitialOwner=TRUE: this call both creates (or opens) the mutex and
    // takes ownership in one step. GetLastError() distinguishes "created
    // fresh" from "already existed" - the latter means another instance
    // is holding it right now.
    HANDLE handle = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    if (!handle) {
        return SingleInstanceLock(nullptr);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(handle);
        return SingleInstanceLock(nullptr);
    }
    return SingleInstanceLock(handle);
}

SingleInstanceLock::SingleInstanceLock(SingleInstanceLock&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

SingleInstanceLock& SingleInstanceLock::operator=(SingleInstanceLock&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            CloseHandle(handle_);
        }
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

SingleInstanceLock::~SingleInstanceLock() {
    if (handle_) {
        ReleaseMutex(handle_);
        CloseHandle(handle_);
    }
}

} // namespace platform_windows
