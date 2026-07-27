#include "cli/CliDispatch.h"
#include "cli/CliHooks.h"

#include "platform/windows/DaemonSignal.h"
#include "platform/windows/ShellLinkAutoLaunch.h"
#include "platform/windows/SmtcMediaSource.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

// The CLI tool is installed next to the main app binary - resolves
// FeatherRPC.exe relative to featherrpc.exe's own directory rather than
// assuming a fixed install path.
std::filesystem::path AppExePath() {
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    return std::filesystem::path(selfPath).parent_path() / L"FeatherRPC.exe";
}

bool SpawnDaemon() {
    std::wstring commandLine = L"\"" + AppExePath().wstring() + L"\" --no-tray";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    // DETACHED_PROCESS: no console attached, survives after this
    // short-lived CLI process exits - the Windows equivalent of the
    // fork+exec+setsid used for the same `daemon start` command on
    // Linux/macOS.
    BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
        DETACHED_PROCESS, nullptr, nullptr, &si, &pi);
    if (!ok) {
        return false;
    }
    CloseHandle(pi.hThread);

    // A healthy headless instance runs until told to stop - it should
    // never exit on its own within a fraction of a second. This grace
    // period catches the case IsRunning() can't: a tray instance is
    // already running (invisible to IsRunning() - only headless mode
    // registers the named Events it checks), so the spawned process hits
    // its own single-instance guard and exits immediately. Without this,
    // `daemon start` would report success ("Started, but not confirmed
    // running yet.") for a process that had already exited.
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 500);
    bool exitedEarly = waitResult == WAIT_OBJECT_0;
    CloseHandle(pi.hProcess);
    return !exitedEarly;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    cli::Hooks hooks;
    hooks.autoLaunch = std::make_unique<platform_windows::ShellLinkAutoLaunch>(AppExePath());
    hooks.daemonSignal = std::make_unique<platform_windows::WindowsDaemonSignal>();
    hooks.listMediaSources = [] { return platform_windows::SmtcMediaSource::GetAvailableSources(); };
    hooks.spawnDaemon = SpawnDaemon;

    return cli::Run(args, hooks);
}
