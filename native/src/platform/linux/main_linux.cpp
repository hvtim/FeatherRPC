#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/DiagnosticReport.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"
#include "core/Version.h"

#include "cli/StatusFile.h"

#include "platform/linux/SniTray.h"
#include "platform/linux/CurlAlbumArtLookup.h"
#include "platform/linux/DesktopAutoLaunch.h"
#include "platform/linux/MprisMediaSource.h"
#include "platform/linux/TextPrompt.h"
#include "platform/posix/CrashHandler.h"
#include "platform/posix/DaemonSignal.h"
#include "platform/posix/UnixSocketIpcTransport.h"

#include <curl/curl.h>
#include <sys/utsname.h>

#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Unlike Windows (which always has "iTunes" as a fixed, always-available
// source), Linux has no source until the user actually picks a live MPRIS
// player from the tray menu - an empty/stale id here just means nothing
// is selected yet, which PresenceEngine already handles via a null
// MediaSource (reports "Nothing playing" instead of crashing).
std::unique_ptr<core::MediaSource> MakeMediaSource(const std::string& id) {
    if (id.empty() || id == "iTunes") {
        return nullptr;
    }
    return std::make_unique<platform_linux::MprisMediaSource>(id);
}

std::string OsDescription() {
    struct utsname uts;
    if (uname(&uts) != 0) {
        return "Linux (version unknown)";
    }
    return std::string(uts.sysname) + " " + uts.release + " " + uts.machine;
}

// Called only from Copy Diagnostic Info, never from the poll loop - the
// actual answer to "how do we tell 'wrong source picked' apart from 'the
// app is broken'": re-queries live MPRIS state right at report-generation
// time instead of depending on historical log noise.
std::string LiveMediaSourceCheck(const core::AppConfig& config) {
    std::ostringstream out;
    if (config.mediaSource.empty()) {
        out << "No media source is currently selected.";
    } else {
        auto source = MakeMediaSource(config.mediaSource);
        bool found = source && source->GetCurrentTrack().has_value();
        out << "Currently selected media source: " << config.mediaSource << ". Live check: "
            << (found ? "found an active session for it right now." : "no active session found for it right now.");
    }

    auto available = platform_linux::MprisMediaSource::GetAvailableSources();
    out << " Available MPRIS players right now: ";
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

bool HasNoTrayFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-tray") {
            return true;
        }
    }
    return false;
}

// Mirrors AppConfig.h's own default for AppConfig::clientId, and
// PresenceEngine.cpp's private kPlaceholderClientId - kept as a separate
// copy here (rather than a shared header constant) because that's how the
// existing PresenceEngine.cpp copy already does it; not worth a shared
// header for one literal used in two translation units.
constexpr const char* kPlaceholderClientId = "YOUR_DISCORD_CLIENT_ID_HERE";

} // namespace

int main(int argc, char** argv) {
    // libcurl's global init is not thread-safe and must run before any
    // other thread (including PresenceEngine's worker) touches curl.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    core::Log::Init(core::GetLogFilePath());
    core::Log::Write(std::string("FeatherRPC starting... (") + core::kBuildString + ")");

    // As early as possible after Log::Init - the crash file descriptor and
    // sigaltstack are both set up here, up front, so the handler itself
    // never has to do anything beyond strictly async-signal-safe work.
    // See CrashHandler.cpp for the full design rationale.
    platform_posix::InstallCrashHandler();

    // First thing, before touching config/engine/tray - refuses to start
    // a second instance (tray or headless) alongside one that's already
    // running, no matter what launched it (systemd, autostart, a manual
    // second run, or `featherrpc daemon start`).
    auto singleInstanceLock = platform_posix::SingleInstanceLock::TryAcquire();
    if (!singleInstanceLock.Acquired()) {
        core::Log::Write("[error] FeatherRPC is already running - exiting.");
        // 75 (not 1) so featherrpc.service's RestartPreventExitStatus=75
        // can tell this intentional, expected refusal apart from an actual
        // crash - otherwise systemd's Restart=on-failure would spin up
        // restart attempts in a tight loop for as long as another
        // instance keeps holding the lock.
        return 75;
    }

    core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());
    std::string currentMediaSourceId = config.mediaSource;
    core::Log::SetVerbose(config.verboseLogging);

    core::PresenceEngine engine(
        config,
        MakeMediaSource(currentMediaSourceId),
        std::make_unique<platform_linux::CurlAlbumArtLookup>(),
        [] { return std::make_unique<platform_posix::UnixSocketIpcTransport>(); });

    // Shared by both the tray's OnConfigChanged and the headless reload
    // loop below - identical either way.
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

    if (HasNoTrayFlag(argc, argv)) {
        // No GLib main loop/tray in this mode - platform_posix's sigwait
        // loop is the entire event loop, replacing the SNI/dbusmenu
        // D-Bus dispatch the same way the Windows daemon path replaces
        // the tray's message pump.
        platform_posix::DaemonBlockSignalsAndWritePidFile();
        engine.OnStatusChanged = [&] { cli::WriteStatusFile(engine.Status()); };
        engine.Start();
        core::Log::Write("Running headless (--no-tray).");

        for (;;) {
            if (platform_posix::DaemonWaitForSignal() == platform_posix::DaemonSignalKind::Quit) {
                break;
            }
            applyConfig(core::LoadConfig(core::GetConfigFilePath()));
            core::Log::Write("Reloaded config.");
        }

        engine.Stop();
        platform_posix::DaemonRemovePidFile();
        core::Log::Write("Exiting.");
        curl_global_cleanup();
        return 0;
    }

    // Mode-agnostic pidfile: written here too (not just in the headless
    // branch above) so `featherrpc status`/`appid set`/etc. always find a
    // running instance regardless of which mode it's actually in - no
    // special-casing in the CLI dispatcher for "is the target headless or
    // tray-mode?" because there's nothing left for it to detect. Deliberately
    // NOT DaemonBlockSignalsAndWritePidFile() - that also blocks
    // SIGHUP/SIGTERM/SIGINT for a sigwait() loop, which would starve
    // SniTray's g_unix_signal_add below (see RunMessageLoop()).
    platform_posix::DaemonWritePidFile();

    platform_linux::DesktopAutoLaunch autoLaunch;

    nativeui::SniTray tray;
    if (!tray.Create()) {
        core::Log::Write("[error] Failed to create the tray icon.");
        platform_posix::DaemonRemovePidFile();
        return 1;
    }
    tray.SetInitialState(config, autoLaunch.IsEnabled());

    // First-run onboarding: users testing the tray-only (no window) build
    // weren't sure it had started, or that a Discord Application ID even
    // needed to be set. One notification, once per launch (not persisted -
    // simply re-shown every time clientId is still the placeholder, same
    // trigger as the Windows/macOS companions in #31/#32), closes both
    // gaps without adding any state to track "already shown".
    if (config.clientId.empty() || config.clientId == kPlaceholderClientId) {
        tray.ShowFirstRunNotification();
    }

    // Mirrors the headless reload loop below, but triggered by SniTray's
    // own g_unix_signal_add(SIGHUP, ...) instead of a sigwait() loop (see
    // RunMessageLoop()) - this is the actual live-reload-while-tray-is-
    // running path the CLI was missing entirely before this change.
    tray.OnReloadRequested = [&] {
        core::AppConfig newConfig = core::LoadConfig(core::GetConfigFilePath());
        config = newConfig;
        applyConfig(newConfig);
        // Unlike OnConfigChanged (which fires from inside the tray's own
        // menu handling, where its internal config_ is already current),
        // this reload originates externally from the CLI - push the new
        // config into the tray's own display state too, so the menu's
        // checkboxes reflect it next time it's opened instead of showing
        // stale values.
        tray.SetInitialState(config, autoLaunch.IsEnabled());
        core::Log::Write("Reloaded config.");
    };

    tray.OnConfigChanged = [&](const core::AppConfig& newConfig) {
        if (newConfig.trayEnabled != config.trayEnabled) {
            core::Log::Write("Tray icon preference changed - takes effect next launch.");
        }
        config = newConfig;
        applyConfig(newConfig);
    };

    tray.OnStartAtLoginChanged = [&](bool enabled) { autoLaunch.SetEnabled(enabled); };

    tray.OnEditApplicationId = [&](std::string& value) {
        platform_linux::PromptForText("FeatherRPC", "Discord Application ID:", value);
    };

    tray.OnEditCustomArtUrl = [&](std::string& value) {
        platform_linux::PromptForText("FeatherRPC", "Image URL (512x512 recommended):", value);
    };

    tray.OnEditFallbackImageKey = [&](std::string& value) {
        platform_linux::PromptForText("FeatherRPC", "Fallback image asset key:", value);
    };

    tray.OnRefreshMediaSources = [] { return platform_linux::MprisMediaSource::GetAvailableSources(); };

    tray.OnBuildDiagnosticReport = [&] {
        core::DiagnosticReportInputs inputs;
        inputs.config = config;
        inputs.osDescription = OsDescription();
        inputs.mediaSourceLiveCheckText = LiveMediaSourceCheck(config);
        inputs.recentLogLines = ReadRecentLogLines();
        inputs.lastCrashReportText = ReadCrashReportIfPresent();
        return core::BuildDiagnosticReport(inputs);
    };

    // Both sinks, same as the pidfile above: the tray tooltip for the user,
    // and the status file for `featherrpc status` - previously only the
    // headless branch wrote the latter, so status in tray mode fell back to
    // "no status reported yet" even though the pidfile now proves it's
    // actually running.
    engine.OnStatusChanged = [&] {
        tray.PostStatusUpdate("FeatherRPC - " + engine.Status());
        cli::WriteStatusFile(engine.Status());
    };

    engine.Start();
    int exitCode = tray.RunMessageLoop();
    engine.Stop();
    platform_posix::DaemonRemovePidFile();

    core::Log::Write("Exiting.");
    curl_global_cleanup();
    return exitCode;
}
