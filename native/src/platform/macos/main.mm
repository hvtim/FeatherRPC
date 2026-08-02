#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/DiagnosticReport.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"
#include "core/Version.h"

#include "AppleSearchAlbumArtLookup.h"
#include "LaunchAgentAutoLaunch.h"
#include "MediaRemoteSource.h"
#include "MusicMediaSource.h"
#include "StatusItemTray.h"
#include "TextPrompt.h"
#include "cli/StatusFile.h"
#include "platform/posix/CrashHandler.h"
#include "platform/posix/DaemonSignal.h"
#include "platform/posix/UnixSocketIpcTransport.h"

#import <Cocoa/Cocoa.h>

#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// "Music" (or the shared cross-platform default "iTunes", or empty) all
// mean Music.app via Scripting Bridge - same reserved-literal pattern
// Linux uses for its own "iTunes" sentinel. Only "MediaRemote" opts into
// the any-app source.
std::unique_ptr<core::MediaSource> MakeMediaSource(const std::string& id) {
    if (id == "MediaRemote") {
        return std::make_unique<platform_macos::MediaRemoteSource>();
    }
    return std::make_unique<platform_macos::MusicMediaSource>();
}

// uname() gives the Darwin kernel version (e.g. "23.5.0"), not the
// marketing macOS version (e.g. "14.5") most bug reports actually need -
// NSProcessInfo's operatingSystemVersion is the documented way to get the
// latter. Both are included since a maintainer symbolizing a crash
// against a specific SDK/build cares about the Darwin number too.
std::string OsDescription() {
    NSOperatingSystemVersion v = [[NSProcessInfo processInfo] operatingSystemVersion];
    struct utsname uts;
    std::string kernelRelease = (uname(&uts) == 0) ? uts.release : "unknown";

    char machine[256] = {};
    size_t size = sizeof(machine);
    std::string arch = (sysctlbyname("hw.machine", machine, &size, nullptr, 0) == 0) ? machine : "unknown arch";

    std::ostringstream out;
    out << "macOS " << v.majorVersion << "." << v.minorVersion << "." << v.patchVersion
        << " (Darwin " << kernelRelease << "), " << arch;
    return out.str();
}

// Called only from Copy Diagnostic Info, never from the poll loop - the
// actual answer to "how do we tell 'wrong source picked' apart from 'the
// app is broken'": re-queries live MediaRemote/Music.app state right at
// report-generation time instead of depending on historical log noise.
std::string LiveMediaSourceCheck(const core::AppConfig& config) {
    std::ostringstream out;
    auto source = MakeMediaSource(config.mediaSource);
    bool found = source && source->GetCurrentTrack().has_value();
    std::string label = config.mediaSource.empty() ? "Music" : config.mediaSource;
    out << "Currently selected media source: " << label << ". Live check: "
        << (found ? "found an active session for it right now." : "no active session found for it right now.");

    auto available = platform_macos::MediaRemoteSource::GetAvailableSources();
    out << " Available sources right now: ";
    if (available.empty()) {
        out << "(none)";
    } else {
        bool first = true;
        for (const auto& src : available) {
            if (!first) out << ", ";
            out << src.displayName;
            first = false;
        }
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
        // InstallCrashHandler() creates this file on every single launch
        // (O_CREAT, so a genuine crash from a *previous* session survives
        // to be read here) - an empty file just means the file has always
        // existed but nothing has ever actually crashed, not a crash with
        // no content. Treating that as "no report" is what makes the
        // trailing section disappear entirely on a machine that's never
        // crashed, instead of showing up with a confusing blank body.
        return std::nullopt;
    }
    return content;
}

} // namespace

int main(int argc, const char** argv) {
    bool noTray = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-tray") {
            noTray = true;
        }
    }

    @autoreleasepool {
        core::Log::Init(core::GetLogFilePath());
        core::Log::Write(std::string("FeatherRPC starting... (") + core::kBuildString + ")");

        // As early as possible after Log::Init - the crash file
        // descriptor and sigaltstack are both set up here, up front, so
        // the handler itself never has to do anything beyond strictly
        // async-signal-safe work. See CrashHandler.cpp (platform/posix/,
        // shared with Linux) for the full design rationale.
        platform_posix::InstallCrashHandler();

        // First thing, before touching config/engine/tray - refuses to
        // start a second instance (tray or headless) alongside one
        // that's already running, no matter what launched it (a
        // LaunchAgent, a manual second run, or `featherrpc-cli daemon
        // start`).
        auto singleInstanceLock = platform_posix::SingleInstanceLock::TryAcquire();
        if (!singleInstanceLock.Acquired()) {
            core::Log::Write("[error] FeatherRPC is already running - exiting.");
            // 75, not 1 - same convention as the Linux build (see
            // featherrpc.service's RestartPreventExitStatus=75), so any
            // exit code inspection stays consistent across platforms.
            return 75;
        }

        core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());
        std::string currentMediaSourceId = config.mediaSource;
        core::Log::SetVerbose(config.verboseLogging);

        platform_macos::LaunchAgentAutoLaunch autoLaunch;

        core::PresenceEngine engine(
            config,
            MakeMediaSource(currentMediaSourceId),
            std::make_unique<platform_macos::AppleSearchAlbumArtLookup>(),
            [] { return std::make_unique<platform_posix::UnixSocketIpcTransport>(); });

        // Shared by both the tray's OnConfigChanged and the headless
        // reload loop below - identical either way.
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
            // No NSApplication/StatusItemTray at all in this path - the
            // sigwait loop below is the entire event loop, replacing the
            // tray's Cocoa run loop the same way the Linux daemon path
            // replaces AppIndicator's glib loop.
            platform_posix::DaemonBlockSignalsAndWritePidFile();

            engine.OnStatusChanged = [&] {
                cli::WriteStatusFile(engine.Status());
            };

            engine.Start();
            core::Log::Write("Running headless (--no-tray).");

            for (;;) {
                auto signal = platform_posix::DaemonWaitForSignal();
                if (signal == platform_posix::DaemonSignalKind::Reload) {
                    core::AppConfig reloaded = core::LoadConfig(core::GetConfigFilePath());
                    applyConfig(reloaded);
                    core::Log::Write("Reloaded config.");
                } else {
                    break;
                }
            }

            engine.Stop();
            platform_posix::DaemonRemovePidFile();
            core::Log::Write("Exiting.");
            return 0;
        }

        [NSApplication sharedApplication];
        // .accessory: no Dock icon, no app menu bar - tray-icon-only
        // footprint, matching the Windows build's hidden-window approach.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        nativeui::StatusItemTray tray;
        if (!tray.Create()) {
            core::Log::Write("Failed to create the status item.");
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

        tray.OnEditApplicationId = [&](std::string& value) {
            nativeui::PromptForText("FeatherRPC", "Discord Application ID:", value);
        };

        tray.OnEditCustomArtUrl = [&](std::string& value) {
            nativeui::PromptForText("FeatherRPC", "Image URL (512x512 recommended):", value);
        };

        tray.OnEditFallbackImageKey = [&](std::string& value) {
            nativeui::PromptForText("FeatherRPC", "Fallback image asset key:", value);
        };

        tray.OnBuildDiagnosticReport = [&] {
            core::DiagnosticReportInputs inputs;
            inputs.config = config;
            inputs.osDescription = OsDescription();
            inputs.mediaSourceLiveCheckText = LiveMediaSourceCheck(config);
            inputs.recentLogLines = ReadRecentLogLines();
            inputs.lastCrashReportText = ReadCrashReportIfPresent();
            return core::BuildDiagnosticReport(inputs);
        };

        engine.OnStatusChanged = [&] {
            tray.PostStatusUpdate(engine.Status());
        };

        engine.Start();
        int exitCode = tray.RunMessageLoop();
        engine.Stop();

        core::Log::Write("Exiting.");
        return exitCode;
    }
}
