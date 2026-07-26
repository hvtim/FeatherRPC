#pragma once

#include "core/AppConfig.h"
#include "core/MediaSource.h"

#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>
#include <vector>

namespace nativeui {

// Shell_NotifyIcon-based tray icon + a native popup menu (TrackPopupMenu)
// standing in for the whole "Settings window" - see the plan's UI Layer
// section. No GUI toolkit involved anywhere in this class. Holds a
// core::AppConfig directly (rather than a separate UI-only state struct)
// so menu actions translate straight into what main.cpp needs to persist
// and hand to PresenceEngine.
class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HINSTANCE hInstance, const std::wstring& tooltip);
    void SetTooltip(const std::wstring& text);

    // Seeds the menu's checkable/radio state from the loaded config and
    // AutoLaunch state - call once after Create(), before showing the menu.
    void SetInitialState(const core::AppConfig& config, bool startAtLogin);

    // Safe to call from any thread (e.g. a background Discord IPC connect
    // attempt) - marshals onto this window's message queue via
    // PostMessage, exactly the cross-thread notification primitive
    // described in the plan's Threading section for Windows.
    void PostStatusUpdate(const std::wstring& text);

    HWND Hwnd() const { return hwnd_; }

    // Runs the standard Win32 message loop until WM_QUIT (posted when the
    // user picks Exit). Blocks the calling thread.
    int RunMessageLoop();

    // Fired whenever a menu action changes config-relevant state
    // (broadcast, track number, art mode, poll interval, media source,
    // application id, custom art url) - main.cpp saves config.json and
    // hands the new config to PresenceEngine.
    std::function<void(const core::AppConfig&)> OnConfigChanged;

    // Fired specifically for the start-at-login toggle, which is backed
    // by AutoLaunch (a Startup-folder shortcut), not config.json.
    std::function<void(bool)> OnStartAtLoginChanged;

    // Fired when the user picks "Set Discord Application ID..." or
    // "Custom image URL...". Handler shows the prompt and mutates the
    // value in place; leaves it unchanged if the user cancels.
    std::function<void(std::wstring&)> OnEditApplicationId;
    std::function<void(std::wstring&)> OnEditCustomArtUrl;

    // Called synchronously right before the context menu is shown, to
    // refresh the Media Source submenu against currently-active SMTC
    // sessions. Should return quickly - runs on the UI thread.
    std::function<std::vector<core::MediaSourceInfo>()> OnRefreshMediaSources;

private:
    static constexpr UINT WM_TRAYICON = WM_APP + 1;
    static constexpr UINT WM_STATUSUPDATE = WM_APP + 2;

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ShowContextMenu();
    HMENU BuildMenu();
    void HandleCommand(UINT id);
    void NotifyConfigChanged();

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    HICON icon_ = nullptr; // owned - freed with DestroyIcon in the destructor
    NOTIFYICONDATAW nid_{};
    UINT taskbarCreatedMsg_ = 0;

    core::AppConfig config_;
    bool startAtLogin_ = false;
    std::vector<core::MediaSourceInfo> mediaSources_{{"iTunes", "iTunes"}};
    std::vector<int> pollIntervalPresetsMs_{1000, 2000, 5000, 10000};
};

} // namespace nativeui
