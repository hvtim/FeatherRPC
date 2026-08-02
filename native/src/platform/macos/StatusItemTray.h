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

    // Assembles the tray's "Copy Diagnostic Info" text - wired from
    // main.mm the same way the Edit* callbacks above are. Called
    // synchronously from HandleCommand() on the main thread.
    std::function<std::string()> OnBuildDiagnosticReport;

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
