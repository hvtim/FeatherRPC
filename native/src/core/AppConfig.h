#pragma once

#include <filesystem>
#include <string>

namespace core {

struct AppConfig {
    std::string clientId = "YOUR_DISCORD_CLIENT_ID_HERE";

    // Must match a Rich Presence Art Asset uploaded to this Discord
    // application (Developer Portal > Rich Presence > Art Assets) under
    // this exact name. See docs/AlbumArt.md.
    std::string largeImageKey = "fallback";

    int pollIntervalMs = 2000;
    bool broadcastEnabled = true;
    bool showTrackNumber = true;

    // "Auto" (lookup), "Custom" (always customArtUrl), or "Off" (always
    // largeImageKey, no lookups).
    std::string artMode = "Auto";
    std::string customArtUrl;

    // Windows: "iTunes" (COM automation), or an SMTC app user model id
    // (e.g. "vlc.exe") for any other app. Linux: an MPRIS bus name (e.g.
    // "org.mpris.MediaPlayer2.vlc"), or empty/"iTunes" for "nothing
    // selected yet". macOS: "Music" (or empty/"iTunes", Scripting
    // Bridge) or "MediaRemote" (any app, via the MediaRemote-adapter
    // workaround - see platform/macos/MediaRemoteSource.h).
    std::string mediaSource = "iTunes";

    // Whether this app instance should create a tray icon. Cannot be
    // applied live to a running process (a running instance can't cleanly
    // make its own tray icon disappear mid-session) - takes effect next
    // launch, unlike every other field here.
    bool trayEnabled = true;

    // Tray Settings submenu toggle - gates extra per-poll detail in
    // PresenceEngine and the media-source implementations via
    // core::Log::IsVerbose(). Log-file-only; no separate debug window.
    bool verboseLogging = false;
};

// Missing/unreadable/corrupt file returns default-constructed AppConfig
// rather than throwing - there's always a sensible config to run with.
AppConfig LoadConfig(const std::filesystem::path& path);
void SaveConfig(const AppConfig& config, const std::filesystem::path& path);

} // namespace core
