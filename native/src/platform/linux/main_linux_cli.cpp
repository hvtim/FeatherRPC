#include "cli/CliDispatch.h"
#include "cli/CliHooks.h"

#include "platform/linux/MprisMediaSource.h"
#include "platform/linux/SystemdUserAutoLaunch.h"
#include "platform/posix/DaemonSignal.h"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// The CLI tool is deliberately installed elsewhere from the main binary
// (~/.local/bin, so it's on $PATH - see install.sh), so unlike macOS/
// Windows there's no way to find FeatherRPC relative to this binary's own
// location. Checks every layout this app can be installed under: a
// distro package (AUR/COPR/PPA - /usr/bin), a manual `cmake --install`
// (/usr/local/bin), and install.sh's own per-user layout
// ($XDG_DATA_HOME/FeatherRPC, matching its DATA_HOME/INSTALL_DIR exactly -
// the one place this needs updating if that layout ever changes). First
// one that actually exists wins; the per-user layout is the fallback if
// none do, same guess this always made before distro packaging existed.
std::filesystem::path AppExePath() {
    for (const char* candidate : {"/usr/bin/FeatherRPC", "/usr/local/bin/FeatherRPC"}) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    const char* dataHome = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base;
    if (dataHome && *dataHome) {
        base = dataHome;
    } else {
        const char* home = std::getenv("HOME");
        base = std::filesystem::path(home ? home : "") / ".local/share";
    }
    return base / "FeatherRPC" / "FeatherRPC";
}

bool SpawnDaemon() {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        // Detach fully - setsid so this isn't killed by the CLI's own
        // terminal session ending, same intent as Windows' DETACHED_PROCESS.
        setsid();
        std::string exe = AppExePath().string();
        execl(exe.c_str(), exe.c_str(), "--no-tray", static_cast<char*>(nullptr));
        std::exit(127); // execl only returns on failure
    }

    // A healthy headless instance runs until told to stop - it should
    // never exit on its own within a fraction of a second. IsRunning()
    // already catches "something's already running" before this point on
    // Linux (tray mode writes the pidfile too), so this is defense in
    // depth against any other early-exit failure, same check as the
    // Windows/macOS builds have for their own (real) discoverability gap.
    for (int i = 0; i < 10; ++i) {
        int status = 0;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    cli::Hooks hooks;
    hooks.autoLaunch = std::make_unique<platform_linux::SystemdUserAutoLaunch>();
    hooks.daemonSignal = std::make_unique<platform_posix::PosixDaemonSignal>();
    hooks.listMediaSources = [] { return platform_linux::MprisMediaSource::GetAvailableSources(); };
    hooks.spawnDaemon = SpawnDaemon;

    return cli::Run(args, hooks);
}
