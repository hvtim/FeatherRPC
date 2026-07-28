#pragma once

#include "core/AppConfig.h"
#include "core/MediaSource.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nativeui {

// NSStatusItem + NSMenu tray icon - the plan's "no GUI toolkit" UI layer,
// same role as platform/windows/TrayIcon but built on Cocoa's native menu
// instead of TrackPopupMenu. Holds a core::AppConfig directly so menu
// actions translate straight into what main.mm needs to persist and hand
// to PresenceEngine.
class StatusItemTray {
public:
    StatusItemTray();
    ~StatusItemTray();

    StatusItemTray(const StatusItemTray&) = delete;
    StatusItemTray& operator=(const StatusItemTray&) = delete;

    bool Create();

    // Seeds the menu's checkable state from the loaded config and
    // AutoLaunch state - call once after Create(), before the menu is
    // ever shown.
    void SetInitialState(const core::AppConfig& config, bool startAtLogin);

    // Shows a one-time NSUserNotification confirming the app is running
    // and pointing at the one required setup step (setting a Discord
    // Application ID). main.mm calls this once, right after the tray is
    // created, only while config.clientId is still the placeholder
    // default - never persisted, so it naturally stops showing once
    // `featherrpc-cli appid set` has been used. Uses the deprecated
    // NSUserNotificationCenter rather than UNUserNotificationCenter: the
    // latter needs a proper bundle identifier plus an entitlement/
    // framework link and can trigger a user permission prompt, which
    // isn't the minimal-footprint choice here - NSUserNotificationCenter
    // works with zero new frameworks/linker flags from code already
    // linking Cocoa.
    void ShowFirstRunNotification();

    // Safe to call from any thread - marshals onto the main thread via
    // dispatch_async(dispatch_get_main_queue(), ...), the Cocoa
    // equivalent of the Windows TrayIcon's PostMessage-based marshaling.
    void PostStatusUpdate(const std::string& text);

    // Runs NSApplication's own event loop until Exit is chosen. Must be
    // called from the main thread - NSStatusItem/NSMenu are main-thread-only.
    int RunMessageLoop();

    std::function<void(const core::AppConfig&)> OnConfigChanged;
    std::function<void(bool)> OnStartAtLoginChanged;
    std::function<void(std::string&)> OnEditApplicationId;
    std::function<void(std::string&)> OnEditCustomArtUrl;
    std::function<void(std::string&)> OnEditFallbackImageKey;

    // Invoked by the Objective-C target/action glue in StatusItemTray.mm
    // when a menu item fires - not meant to be called from main.mm.
    void HandleCommand(int commandId);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    core::AppConfig _config;
    bool _startAtLogin = false;
    std::vector<core::MediaSourceInfo> _mediaSources;

    void RebuildMenu();
    void NotifyConfigChanged();
};

} // namespace nativeui
