#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/DiagnosticReport.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"
#include "core/Version.h"

#include "cli/StatusFile.h"

#include "platform/windows/CrashHandler.h"
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
#include <shobjidl.h>

#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::unique_ptr<core::MediaSource> MakeMediaSource(const std::string& id) {
    if (id == "iTunes") {
        return std::make_unique<platform_windows::ITunesMediaSource>();
    }
    return std::make_unique<platform_windows::SmtcMediaSource>(id);
}

// RtlGetVersion, not GetVersionExW - the latter lies about the true OS
// version unless the exe manifest declares compatibility GUIDs for each
// Windows release, which this app's manifest doesn't. Windows 11 still
// reports dwMajorVersion 10 internally - distinguished here the same way
// most diagnostic tools do, by build number (22000+).
std::string OsDescription() {
    std::string osName = "Windows (version unknown)";
    DWORD buildNumber = 0;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion) {
            OSVERSIONINFOEXW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtlGetVersion(&info) == 0) {
                buildNumber = info.dwBuildNumber;
                if (info.dwMajorVersion == 10 && buildNumber >= 22000) {
                    osName = "Windows 11";
                } else if (info.dwMajorVersion == 10) {
                    osName = "Windows 10";
                } else {
                    osName = "Windows " + std::to_string(info.dwMajorVersion);
                }
            }
        }
    }

    SYSTEM_INFO sysInfo{};
    GetNativeSystemInfo(&sysInfo);
    const char* arch = "unknown arch";
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "ARM64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86"; break;
    }

    return osName + " (build " + std::to_string(buildNumber) + "), " + arch;
}

// Called only from Copy Diagnostic Info - an on-demand check, not logged
// per poll, stating whether the selected source has anything active and
// what else is available.
std::string LiveMediaSourceCheck(const core::AppConfig& config) {
    std::ostringstream out;
    out << "Currently selected media source: " << config.mediaSource << ". ";

    auto source = MakeMediaSource(config.mediaSource);
    auto track = source->GetCurrentTrack();
    bool foundActive = track.has_value() && !track->name.empty() && track->state != core::PlaybackState::Stopped;
    out << "Live check: " << (foundActive ? "found an active track right now." : "no active track found right now for this source.");

    auto available = platform_windows::SmtcMediaSource::GetAvailableSources();
    out << " Available SMTC sources right now: ";
    if (available.empty()) {
        out << "none.";
    } else {
        for (size_t i = 0; i < available.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << available[i].displayName;
        }
        out << ".";
    }
    return out.str();
}

std::vector<std::string> ReadRecentLogLines(size_t maxLines = 50) {
    std::ifstream file(core::GetLogFilePath());
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(std::move(line));
        if (lines.size() > maxLines) {
            lines.pop_front();
        }
    }
    return std::vector<std::string>(lines.begin(), lines.end());
}

std::optional<std::string> ReadCrashReportIfPresent() {
    std::ifstream file(core::GetCrashFilePath(), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << file.rdbuf();
    std::string content = out.str();
    if (content.empty()) {
        // The file exists on every launch (OPEN_ALWAYS) even with no
        // crash - an empty file means no crash, not an empty report.
        return std::nullopt;
    }
    return content;
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
    // Needed for the WinRT toast-notification permission check
    // (ToastPermission.cpp, used by TrayIcon's first-run balloon) to work
    // at all - must match the AppUserModelID set on the Start Menu
    // shortcut in installer.nsi's CreateShortcut call.
    SetCurrentProcessExplicitAppUserModelID(L"FeatherRPC.TrayApp");

    bool noTray = HasNoTrayFlag();
    AttachDebugConsole();

    core::Log::Init(core::GetLogFilePath());
    core::Log::Write(std::string("FeatherRPC starting... (") + core::kBuildString + ")");

    // As early as possible so the crash filter never has to open its own
    // file handle later. See CrashHandler.cpp.
    platform_windows::InstallCrashHandler();

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
    core::Log::SetVerbose(config.verboseLogging);

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
        core::Log::SetVerbose(newConfig.verboseLogging);

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

    // Still on the placeholder default from a fresh install/config -
    // nudge the tester that the app is actually running and point at the
    // one required setup step. Compared against a default-constructed
    // AppConfig rather than a hardcoded literal so this doesn't become a
    // third copy of the placeholder string (see AppConfig.h and
    // PresenceEngine.cpp's kPlaceholderClientId).
    if (config.clientId == core::AppConfig{}.clientId) {
        tray.ScheduleFirstRunBalloon();
    }

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

    tray.OnBuildDiagnosticReport = [&] {
        core::DiagnosticReportInputs inputs;
        inputs.config = config;
        inputs.osDescription = OsDescription();
        inputs.mediaSourceLiveCheckText = LiveMediaSourceCheck(config);
        inputs.recentLogLines = ReadRecentLogLines();
        inputs.lastCrashReportText = ReadCrashReportIfPresent();
        return platform_windows::WideFromNarrow(core::BuildDiagnosticReport(inputs));
    };

    engine.OnStatusChanged = [&] {
        tray.PostStatusUpdate(L"FeatherRPC - " + platform_windows::WideFromNarrow(engine.Status()));
    };

    engine.Start();
    int exitCode = tray.RunMessageLoop();
    engine.Stop();

    core::Log::Write("Exiting.");
    return exitCode;
}
