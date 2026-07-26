#include "DaemonSignal.h"

#include "core/ConfigPaths.h"

#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <optional>

namespace platform_posix {

namespace {

std::optional<pid_t> ReadPidFile() {
    std::ifstream file(core::GetPidFilePath());
    if (!file) {
        return std::nullopt;
    }
    long pid = 0;
    file >> pid;
    if (!file || pid <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(pid);
}

} // namespace

bool PosixDaemonSignal::IsRunning() const {
    auto pid = ReadPidFile();
    if (!pid) {
        return false;
    }
    // Signal 0 sends nothing but still validates the pid is signalable -
    // ESRCH means the process is gone (stale pidfile from a crash); EPERM
    // means it exists but belongs to another user, which still counts as
    // "something is there" for this check.
    return kill(*pid, 0) == 0 || errno == EPERM;
}

bool PosixDaemonSignal::RequestReload() {
    auto pid = ReadPidFile();
    return pid && kill(*pid, SIGHUP) == 0;
}

bool PosixDaemonSignal::RequestQuit() {
    auto pid = ReadPidFile();
    return pid && kill(*pid, SIGTERM) == 0;
}

void DaemonBlockSignalsAndWritePidFile() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    // Blocked here, before PresenceEngine's worker thread is spawned, so
    // the mask is inherited by every thread in the process - a signal
    // handler running arbitrary C++ (allocations, mutex locks) on an
    // arbitrary thread is undefined behavior, which is why this uses
    // sigwait() on a specific thread instead of signal()/sigaction().
    sigprocmask(SIG_BLOCK, &set, nullptr);

    std::filesystem::create_directories(core::GetConfigDirectory());
    std::ofstream file(core::GetPidFilePath());
    file << static_cast<long>(getpid());
}

DaemonSignalKind DaemonWaitForSignal() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);

    int sig = 0;
    while (sigwait(&set, &sig) != 0) {
        // EINTR or similar - retry.
    }
    return sig == SIGHUP ? DaemonSignalKind::Reload : DaemonSignalKind::Quit;
}

void DaemonRemovePidFile() {
    std::error_code ec;
    std::filesystem::remove(core::GetPidFilePath(), ec);
}

} // namespace platform_posix
