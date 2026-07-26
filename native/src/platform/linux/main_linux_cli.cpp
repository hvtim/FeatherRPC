#include "cli/CliDispatch.h"
#include "cli/CliHooks.h"

#include "platform/linux/MprisMediaSource.h"
#include "platform/linux/SystemdUserAutoLaunch.h"
#include "platform/posix/DaemonSignal.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

// The CLI tool is installed next to the main app binary - resolves
// FeatherRPC relative to featherrpc-cli's own directory rather than
// assuming a fixed install path.
std::filesystem::path AppExePath() {
    char buf[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::filesystem::path self = len > 0 ? std::filesystem::path(std::string(buf, static_cast<size_t>(len)))
                                          : std::filesystem::path();
    return self.parent_path() / "FeatherRPC";
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
