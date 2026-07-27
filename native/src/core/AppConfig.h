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

    // "iTunes" (COM automation), or an SMTC app user model id (e.g.
    // "vlc.exe") for any other app reporting now-playing info to Windows.
    std::string mediaSource = "iTunes";

    // Whether this app instance should create a tray icon. Cannot be
    // applied live to a running process (a running instance can't cleanly
    // make its own tray icon disappear mid-session) - takes effect next
    // launch, unlike every other field here.
    bool trayEnabled = true;
};

// Missing/unreadable/corrupt file returns default-constructed AppConfig
// rather than throwing - there's always a sensible config to run with.
AppConfig LoadConfig(const std::filesystem::path& path);
void SaveConfig(const AppConfig& config, const std::filesystem::path& path);

} // namespace core
