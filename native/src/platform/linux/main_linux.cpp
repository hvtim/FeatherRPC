#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"

#include "platform/linux/AppIndicatorTray.h"
#include "platform/linux/CurlAlbumArtLookup.h"
#include "platform/linux/DesktopAutoLaunch.h"
#include "platform/linux/MprisMediaSource.h"
#include "platform/linux/TextPrompt.h"
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

} // namespace

int main() {
    // libcurl's global init is not thread-safe and must run before any
    // other thread (including PresenceEngine's worker) touches curl.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    core::Log::Init(core::GetLogFilePath());
    core::Log::Write("FeatherRPC starting...");

    core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());
    std::string currentMediaSourceId = config.mediaSource;

    platform_linux::DesktopAutoLaunch autoLaunch;

    core::PresenceEngine engine(
        config,
        MakeMediaSource(currentMediaSourceId),
        std::make_unique<platform_linux::CurlAlbumArtLookup>(),
        [] { return std::make_unique<platform_posix::UnixSocketIpcTransport>(); });

    nativeui::AppIndicatorTray tray;
    if (!tray.Create("featherrpc")) {
        core::Log::Write("[error] Failed to create the tray icon.");
        return 1;
    }
    tray.SetInitialState(config, autoLaunch.IsEnabled());

    tray.OnConfigChanged = [&](const core::AppConfig& newConfig) {
        core::SaveConfig(newConfig, core::GetConfigFilePath());

        std::unique_ptr<core::MediaSource> newMediaSource;
        if (newConfig.mediaSource != currentMediaSourceId) {
            currentMediaSourceId = newConfig.mediaSource;
            newMediaSource = MakeMediaSource(currentMediaSourceId);
        }

        engine.UpdateConfig(newConfig, std::move(newMediaSource));
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
