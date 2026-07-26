#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"

#include "cli/StatusFile.h"

#include "platform/linux/SniTray.h"
#include "platform/linux/CurlAlbumArtLookup.h"
#include "platform/linux/DesktopAutoLaunch.h"
#include "platform/linux/MprisMediaSource.h"
#include "platform/linux/TextPrompt.h"
#include "platform/posix/DaemonSignal.h"
#include "platform/posix/UnixSocketIpcTransport.h"

#include <curl/curl.h>

#include <memory>
#include <string>

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

bool HasNoTrayFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-tray") {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    // libcurl's global init is not thread-safe and must run before any
    // other thread (including PresenceEngine's worker) touches curl.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    core::Log::Init(core::GetLogFilePath());
    core::Log::Write("FeatherRPC starting...");

    core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());
    std::string currentMediaSourceId = config.mediaSource;

    core::PresenceEngine engine(
        config,
        MakeMediaSource(currentMediaSourceId),
        std::make_unique<platform_linux::CurlAlbumArtLookup>(),
        [] { return std::make_unique<platform_posix::UnixSocketIpcTransport>(); });

    // Shared by both the tray's OnConfigChanged and the headless reload
    // loop below - identical either way.
    auto applyConfig = [&](const core::AppConfig& newConfig) {
        core::SaveConfig(newConfig, core::GetConfigFilePath());

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

    platform_linux::DesktopAutoLaunch autoLaunch;

    nativeui::SniTray tray;
    if (!tray.Create("featherrpc")) {
        core::Log::Write("[error] Failed to create the tray icon.");
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

    tray.OnStartAtLoginChanged = [&](bool enabled) { autoLaunch.SetEnabled(enabled); };

    tray.OnEditApplicationId = [&](std::string& value) {
        platform_linux::PromptForText("FeatherRPC", "Discord Application ID:", value);
    };

    tray.OnEditCustomArtUrl = [&](std::string& value) {
        platform_linux::PromptForText("FeatherRPC", "Image URL (512x512 recommended):", value);
    };

    tray.OnRefreshMediaSources = [] { return platform_linux::MprisMediaSource::GetAvailableSources(); };

    engine.OnStatusChanged = [&] { tray.PostStatusUpdate("FeatherRPC - " + engine.Status()); };

    engine.Start();
    int exitCode = tray.RunMessageLoop();
    engine.Stop();

    core::Log::Write("Exiting.");
    curl_global_cleanup();
    return exitCode;
}
