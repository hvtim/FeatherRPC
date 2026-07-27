#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"

#include "cli/StatusFile.h"

#include "platform/windows/DaemonSignal.h"
#include "platform/windows/ITunesMediaSource.h"
#include "platform/windows/PipeIpcTransport.h"
#include "platform/windows/ShellLinkAutoLaunch.h"
#include "platform/windows/SmtcMediaSource.h"
#include "platform/windows/StringConvert.h"
#include "platform/windows/TextPrompt.h"
#include "platform/windows/TrayIcon.h"
#include "platform/windows/WinHttpAlbumArtLookup.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

std::unique_ptr<core::MediaSource> MakeMediaSource(const std::string& id) {
    if (id == "iTunes") {
        return std::make_unique<platform_windows::ITunesMediaSource>();
    }
    return std::make_unique<platform_windows::SmtcMediaSource>(id);
}

// Useful for live debugging while running from an existing terminal.
// AttachConsole(ATTACH_PARENT_PROCESS) only succeeds if the process
// actually has a console-hosting parent (cmd.exe/PowerShell) - launching
// via Explorer/Start Menu/Startup shortcut has no such parent, so this
// is a no-op then, unlike AllocConsole() which would unconditionally pop
// a console for every normal launch. Log::Write also always writes to
// the log file, which is the only way to see anything once the app is
// running windowless via autorun.
void AttachDebugConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    SetConsoleTitleW(L"FeatherRPC - debug console");
}

// wWinMain doesn't get argc/argv directly - CommandLineToArgvW is the
// standard way to recover them from GetCommandLineW().
bool HasNoTrayFlag() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }
    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--no-tray") == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    bool noTray = HasNoTrayFlag();
    AttachDebugConsole();

    core::Log::Init(core::GetLogFilePath());
    core::Log::Write("FeatherRPC starting...");

    // First thing, before touching config/engine/tray - refuses to start
    // a second instance (tray or headless) alongside one that's already
    // running, no matter what launched it (the Startup shortcut, a manual
    // second run, or `featherrpc-cli daemon start`).
    auto singleInstanceLock = platform_windows::SingleInstanceLock::TryAcquire();
    if (!singleInstanceLock.Acquired()) {
        core::Log::Write("[error] FeatherRPC is already running - exiting.");
        // 75, not 1 - same convention as the Linux build, so any exit
        // code inspection stays consistent across platforms even though
        // Windows/macOS have no systemd-equivalent consumer of it today.
        return 75;
    }

    core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());
    std::string currentMediaSourceId = config.mediaSource;

    platform_windows::ShellLinkAutoLaunch autoLaunch;

    core::PresenceEngine engine(
        config,
        MakeMediaSource(currentMediaSourceId),
        std::make_unique<platform_windows::WinHttpAlbumArtLookup>(),
        [] { return std::make_unique<platform_windows::PipeIpcTransport>(); });

    // Shared by both the tray's OnConfigChanged and the headless reload
    // loop below - the "save + re-resolve media source if it changed +
    // hand off to the engine" sequence is identical either way.
    auto applyConfig = [&](const core::AppConfig& newConfig) {
        core::SaveConfig(newConfig, core::GetConfigFilePath());

        std::unique_ptr<core::MediaSource> newMediaSource;
        if (newConfig.mediaSource != currentMediaSourceId) {
            currentMediaSourceId = newConfig.mediaSource;
            newMediaSource = MakeMediaSource(currentMediaSourceId);
        }

        engine.UpdateConfig(newConfig, std::move(newMediaSource));
    };

    if (noTray) {
        // No tray/message-loop thread exists in this mode, so `featherrpc`
        // reaches this process via the named Events in DaemonSignal.h
        // instead of PostMessage.
        platform_windows::DaemonWaiter waiter;
        engine.OnStatusChanged = [&] { cli::WriteStatusFile(engine.Status()); };
        engine.Start();

        for (;;) {
            if (waiter.Wait() == platform_windows::DaemonSignalKind::Quit) {
                break;
            }
            applyConfig(core::LoadConfig(core::GetConfigFilePath()));
            core::Log::Write("Reloaded config.");
        }

        engine.Stop();
        core::Log::Write("Exiting.");
        return 0;
    }

    nativeui::TrayIcon tray;
    if (!tray.Create(hInstance, L"FeatherRPC")) {
        MessageBoxW(nullptr, L"Failed to create the tray icon.", L"FeatherRPC", MB_ICONERROR);
        return 1;
    }
    tray.SetInitialState(config, autoLaunch.IsEnabled());

    tray.OnConfigChanged = [&](const core::AppConfig& newConfig) {
        if (newConfig.trayEnabled != config.trayEnabled) {
            core::Log::Write("Tray icon preference changed - takes effect next launch.");
        }
        config = newConfig;
        applyConfig(newConfig);
    };

    tray.OnStartAtLoginChanged = [&](bool enabled) {
        autoLaunch.SetEnabled(enabled);
    };

    tray.OnEditApplicationId = [&](std::wstring& value) {
        nativeui::PromptForText(tray.Hwnd(), L"FeatherRPC", L"Discord Application ID:", value);
    };

    tray.OnEditCustomArtUrl = [&](std::wstring& value) {
        nativeui::PromptForText(tray.Hwnd(), L"FeatherRPC", L"Image URL (512x512 recommended):", value);
    };

    tray.OnEditFallbackImageKey = [&](std::wstring& value) {
        nativeui::PromptForText(tray.Hwnd(), L"FeatherRPC", L"Fallback image asset key:", value);
    };

    tray.OnRefreshMediaSources = [] { return platform_windows::SmtcMediaSource::GetAvailableSources(); };

    engine.OnStatusChanged = [&] {
        tray.PostStatusUpdate(L"FeatherRPC - " + platform_windows::WideFromNarrow(engine.Status()));
    };

    engine.Start();
    int exitCode = tray.RunMessageLoop();
    engine.Stop();

    core::Log::Write("Exiting.");
    return exitCode;
}
