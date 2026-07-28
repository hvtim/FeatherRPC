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

    // Opens the tray's own dropdown menu once, right after creation, if
    // the app is still unconfigured - the macOS equivalent of the
    // Windows/Linux first-run notifications (#31/#33), but deliberately
    // not a system notification: NSUserNotificationCenter's first-ever
    // call implicitly triggers a permission prompt on Big Sur+, and that
    // first notification is typically dropped rather than shown even if
    // the user allows it: there's no reliable way to guarantee the
    // message actually appears on a genuinely fresh launch. The modern
    // UNUserNotificationCenter's explicit requestAuthorization doesn't
    // help either - Apple requires the app be code-signed for that
    // permission dialog to appear at all, and this app isn't signed yet
    // (see #29). Opening the menu directly is pure in-app UI, no
    // permission involved either way: it confirms the app is running
    // (the menu belongs to a live process) and puts "Set Discord
    // Application ID..." right in front of the user immediately.
    void ShowFirstRunMenu();

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
