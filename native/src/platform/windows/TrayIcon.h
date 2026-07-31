#pragma once

#include "core/AppConfig.h"
#include "core/MediaSource.h"

#include <windows.h>
#include <shellapi.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
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
    std::function<void(std::wstring&)> OnEditFallbackImageKey;

    // Refreshes the Media Source submenu against currently-active SMTC
    // sessions. Called from a dedicated background thread (never the UI
    // thread - see StartMediaSourcesRefreshThread()) shortly after each
    // time the context menu is opened; the menu itself shows whatever was
    // last fetched, updating from the *next* time it's opened onward.
    std::function<std::vector<core::MediaSourceInfo>()> OnRefreshMediaSources;

private:
    static constexpr UINT WM_TRAYICON = WM_APP + 1;
    static constexpr UINT WM_STATUSUPDATE = WM_APP + 2;
    static constexpr UINT WM_MEDIASOURCESUPDATE = WM_APP + 3;

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ShowContextMenu();

    // Builds menu_/sourceMenu_/artMenu_/pollMenu_ once, for the whole life
    // of the tray icon - see menu_'s comment for why. Only call once, from
    // Create().
    void BuildMenuOnce();

    // Applies current config_/startAtLogin_/mediaSources_ state (checked
    // toggles, radio selections) onto the already-built menu_ in place -
    // no allocation, no menu structure changes. Safe to call any time
    // after BuildMenuOnce() has run.
    void SyncMenuState();

    // Clears and repopulates sourceMenu_'s items from mediaSources_ (the
    // one submenu whose item count/content actually changes over time),
    // then calls SyncMenuState() to reapply its radio selection. Called
    // once from BuildMenuOnce() for the initial population, and again
    // whenever a fresh media-source list arrives (WM_MEDIASOURCESUPDATE).
    void RefreshSourceMenuItems();

    void HandleCommand(UINT id);
    void NotifyConfigChanged();

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    HICON icon_ = nullptr; // owned - freed with DestroyIcon in the destructor
    NOTIFYICONDATAW nid_{};
    UINT taskbarCreatedMsg_ = 0;

    // Built once (BuildMenuOnce(), called from Create()) and destroyed
    // once (the destructor), instead of every single right-click - the
    // repeated CreatePopupMenu/AppendMenuW/DestroyMenu churn that used to
    // happen on every click was measurably growing the process's working
    // set under rapid clicking (heap allocator retaining freed memory
    // rather than returning it to the OS). DestroyMenu(menu_) recursively
    // destroys the three attached popup submenus too, so they don't need
    // separate cleanup - kept as separate members (rather than looked up
    // via GetSubMenu + a hardcoded position each time) so a future menu
    // reordering can't silently break which submenu gets updated.
    HMENU menu_ = nullptr;
    HMENU sourceMenu_ = nullptr;
    HMENU artMenu_ = nullptr;
    HMENU pollMenu_ = nullptr;

    core::AppConfig config_;
    bool startAtLogin_ = false;
    std::vector<core::MediaSourceInfo> mediaSources_{{"iTunes", "iTunes"}};
    std::vector<int> pollIntervalPresetsMs_{1000, 2000, 5000, 10000};

    // One long-lived background thread for the Media Source submenu's
    // refresh, instead of spawning a new OS thread per right-click (see
    // ShowContextMenu()'s comment - spawning per click was measurably
    // leaking a little memory under rapid clicking, from thread/COM
    // create+teardown overhead, even once the underlying WinRT deadlock
    // itself was fixed). Same long-lived-worker-thread shape as
    // PresenceEngine's own thread, which never showed this problem.
    std::thread mediaSourcesRefreshThread_;
    std::mutex mediaSourcesRefreshMutex_;
    std::condition_variable mediaSourcesRefreshCv_;
    bool mediaSourcesRefreshRequested_ = false;
    bool mediaSourcesRefreshShouldExit_ = false;

    void StartMediaSourcesRefreshThread();
    void RequestMediaSourcesRefresh();
};

} // namespace nativeui
