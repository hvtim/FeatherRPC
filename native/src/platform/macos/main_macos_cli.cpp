// Plain C++, not Objective-C++ - the featherrpc-cli tool never touches
// Cocoa, so it doesn't need the ARC/Cocoa-framework linkage the main app
// target requires. Only .mm compilation is enabled project-wide for the
// files that actually need it.
#include "cli/CliDispatch.h"
#include "cli/CliHooks.h"

#include "LaunchAgentAutoLaunch.h"
#include "MediaRemoteSource.h"
#include "platform/posix/DaemonSignal.h"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// Matches installer/macos/install.sh's INSTALL_DIR/APP_NAME exactly -
// the one place this needs updating if the install location ever
// changes. There's no other reliable way for this separately-installed
// CLI binary to find the .app bundle's inner executable.
std::string InstalledAppExePath() {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : "";
    return base + "/Applications/FeatherRPC.app/Contents/MacOS/FeatherRPC";
}

bool SpawnDaemon() {
    std::string exePath = InstalledAppExePath();
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        // Detach into its own session so it survives this CLI process
        // exiting, then exec the real app headless.
        setsid();
        execl(exePath.c_str(), exePath.c_str(), "--no-tray", static_cast<char*>(nullptr));
        _exit(127); // execl only returns on failure
    }

    // A healthy headless instance runs until told to stop - it should
    // never exit on its own within a fraction of a second. This grace
    // period catches the case IsRunning() can't: tray mode never writes
    // the pidfile IsRunning() checks, so if a tray instance is already
    // running, the spawned process here hits its own single-instance
    // guard and exits immediately. Without this, `daemon start` would
    // report success for a process that had already exited.
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
    hooks.autoLaunch = std::make_unique<platform_macos::LaunchAgentAutoLaunch>(InstalledAppExePath());
    hooks.daemonSignal = std::make_unique<platform_posix::PosixDaemonSignal>();
    // Fixed 2-entry list (Music.app, plus the MediaRemote-adapter-backed
    // "any app" source) - see MediaRemoteSource.h for why there's nothing
    // to dynamically enumerate here, unlike Windows/Linux.
    hooks.listMediaSources = [] { return platform_macos::MediaRemoteSource::GetAvailableSources(); };
    // Prints the exact id string `mediasource set` expects - lets a user
    // discover the right bundle id to filter to without it ever being
    // enumerable up front (see MediaRemoteSource::GetCurrentBundleId).
    hooks.currentMediaSource = [] {
        auto bundleId = platform_macos::MediaRemoteSource::GetCurrentBundleId();
        if (!bundleId.has_value()) {
            return std::optional<std::string>();
        }
        return std::optional<std::string>("MediaRemote:" + *bundleId);
    };
    hooks.spawnDaemon = SpawnDaemon;

    return cli::Run(args, hooks);
}
