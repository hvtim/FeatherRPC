#include "TrayIcon.h"
#include "DarkMode.h"
#include "StringConvert.h"
#include "resource.h"

#include <commctrl.h>

#include <thread>

namespace nativeui {

namespace {

// Command IDs. Dynamic groups (media source, poll interval) reserve a
// block of IDs and recover the index as (id - base) - simple and avoids
// pulling in a std::map just to remember what each ID means.
constexpr UINT CMD_SET_APP_ID = 1;
constexpr UINT CMD_TOGGLE_BROADCAST = 3;
constexpr UINT CMD_TOGGLE_TRACK_NUMBER = 4;
constexpr UINT CMD_TOGGLE_START_AT_LOGIN = 5;
constexpr UINT CMD_EXIT = 6;
constexpr UINT CMD_TOGGLE_TRAY_ENABLED = 7;
constexpr UINT CMD_SET_FALLBACK_KEY = 8;

constexpr UINT CMD_ART_MODE_AUTO = 200;
constexpr UINT CMD_ART_MODE_CUSTOM = 201;
constexpr UINT CMD_ART_MODE_OFF = 202;

constexpr UINT CMD_MEDIA_SOURCE_BASE = 100; // + index, up to 99 sources
constexpr UINT CMD_POLL_INTERVAL_BASE = 300; // + index into presets

const wchar_t* kWindowClassName = L"FeatherRPCNativeTrayWindow";

} // namespace

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    if (mediaSourcesRefreshThread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mediaSourcesRefreshMutex_);
            mediaSourcesRefreshShouldExit_ = true;
        }
        mediaSourcesRefreshCv_.notify_one();
        mediaSourcesRefreshThread_.join();
    }

    if (hwnd_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        DestroyWindow(hwnd_);
    }
    if (icon_) {
        DestroyIcon(icon_);
    }
    if (menu_) {
        DestroyMenu(menu_); // recursively destroys sourceMenu_/artMenu_/pollMenu_ too
    }
    UnregisterClassW(kWindowClassName, hInstance_);
}

bool TrayIcon::Create(HINSTANCE hInstance, const std::wstring& tooltip) {
    hInstance_ = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayIcon::WndProcThunk;
    wc.hInstance = hInstance_;
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    // Not WS_VISIBLE - this window only exists to own the tray icon and
    // receive its callback messages, mirroring the plan's "hidden
    // message-only window" description. Passing `this` through the
    // CREATESTRUCT lets WndProcThunk recover the instance before any
    // other message arrives.
    hwnd_ = CreateWindowExW(
        0, kWindowClassName, L"FeatherRPC", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, hInstance_, this);
    if (!hwnd_) {
        return false;
    }

    EnableDarkModeForMenus(); // undocumented API risk - see its own comment

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;

    // Loads (and rescales if needed) whichever embedded frame in icon.ico
    // best matches the tray's actual small-icon size, rather than
    // decoding the largest frame and scaling it down. icon.ico bakes an
    // exact frame for every standard Windows display-scaling percentage
    // (100%-350%), for rendering quality at whatever scale the user is
    // on - NOT to avoid any loading cost: confirmed (optimize/windows-
    // memory-footprint) that Windows' icon-loading APIs load the same
    // WIC-based codec pipeline regardless of which API is called or
    // whether the source frame is an exact match, so there's no cheaper
    // path to take here.
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (FAILED(LoadIconWithScaleDown(hInstance_, MAKEINTRESOURCEW(IDI_APP_ICON), cx, cy, &icon_))) {
        icon_ = nullptr;
    }
    nid_.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid_.szTip, tooltip.c_str(), _TRUNCATE);

    StartMediaSourcesRefreshThread();
    BuildMenuOnce();

    return Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
}

void TrayIcon::StartMediaSourcesRefreshThread() {
    // One thread, for the whole life of the app, instead of spawning a
    // new one per right-click - see ShowContextMenu()'s comment. Safe to
    // capture `this` here (unlike the per-click approach this replaced):
    // the destructor signals + joins this thread before any other
    // teardown, so `this` stays valid for the thread's entire lifetime.
    mediaSourcesRefreshThread_ = std::thread([this] {
        while (true) {
            std::unique_lock<std::mutex> lock(mediaSourcesRefreshMutex_);
            mediaSourcesRefreshCv_.wait(lock, [this] {
                return mediaSourcesRefreshRequested_ || mediaSourcesRefreshShouldExit_;
            });
            if (mediaSourcesRefreshShouldExit_) {
                return;
            }
            mediaSourcesRefreshRequested_ = false;
            lock.unlock();

            if (!OnRefreshMediaSources) {
                continue;
            }
            // The WinRT GlobalSystemMediaTransportControls call this makes
            // must never run on the UI thread - confirmed (issue #57) to
            // deadlock there even indirectly (e.g. a UI-thread .join() on
            // a worker doing this call also hung). This thread never
            // touches window messages itself, only PostMessageW's the
            // result back, which is what makes it safe - same shape as
            // PresenceEngine's own worker thread, which never hung.
            auto* sources = new std::vector<core::MediaSourceInfo>(OnRefreshMediaSources());
            if (!PostMessageW(hwnd_, WM_MEDIASOURCESUPDATE, 0, reinterpret_cast<LPARAM>(sources))) {
                delete sources;
            }
        }
    });
}

void TrayIcon::RequestMediaSourcesRefresh() {
    {
        std::lock_guard<std::mutex> lock(mediaSourcesRefreshMutex_);
        mediaSourcesRefreshRequested_ = true;
    }
    mediaSourcesRefreshCv_.notify_one();
}

void TrayIcon::SetInitialState(const core::AppConfig& config, bool startAtLogin) {
    config_ = config;
    startAtLogin_ = startAtLogin;
}

void TrayIcon::SetTooltip(const std::wstring& text) {
    wcsncpy_s(nid_.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::ShowFirstRunBalloon() {
    // A separate copy, not a mutation of nid_ - nid_ is shared with every
    // other NIM_MODIFY call (SetTooltip() alone fires every poll interval),
    // so setting NIF_INFO/szInfo directly on it would make those unrelated
    // updates re-carry this same balloon text too, showing it again and
    // again instead of once.
    NOTIFYICONDATAW balloon = nid_;
    balloon.uFlags |= NIF_INFO;
    wcscpy_s(balloon.szInfoTitle, L"FeatherRPC");
    wcscpy_s(balloon.szInfo, L"FeatherRPC is running. Right-click the tray icon to set your Discord Application ID.");
    balloon.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

void TrayIcon::PostStatusUpdate(const std::wstring& text) {
    // Ownership transfers to whichever thread handles WM_STATUSUPDATE
    // (always this window's own message-loop thread) - freed there.
    auto* copy = new std::wstring(text);
    if (!PostMessageW(hwnd_, WM_STATUSUPDATE, 0, reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}

int TrayIcon::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK TrayIcon::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrayIcon* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayIcon*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT TrayIcon::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        // explorer.exe restarted - re-add our icon, it was silently dropped.
        Shell_NotifyIconW(NIM_ADD, &nid_);
        return 0;
    }

    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowContextMenu();
            }
            return 0;
        case WM_STATUSUPDATE: {
            auto* text = reinterpret_cast<std::wstring*>(lParam);
            if (text) {
                SetTooltip(*text);
                delete text;
            }
            return 0;
        }
        case WM_MEDIASOURCESUPDATE: {
            auto* sources = reinterpret_cast<std::vector<core::MediaSourceInfo>*>(lParam);
            if (sources) {
                // ShowContextMenu() wakes the refresh thread on every single
                // click, so this message arrives after every click too -
                // most of the time the set of playing apps hasn't actually
                // changed since last time. Rebuilding sourceMenu_'s items
                // (DeleteMenu+AppendMenuW) is real, repeated work; skipping
                // it when nothing changed is what actually stops the
                // per-click growth, rather than just moving it to a
                // background thread (which only stopped it from blocking
                // the UI, not from happening at all).
                std::vector<core::MediaSourceInfo> updated;
                updated.push_back({"iTunes", "iTunes"});
                for (auto& source : *sources) {
                    updated.push_back(source);
                }
                delete sources;

                if (updated != mediaSources_) {
                    mediaSources_ = std::move(updated);
                    RefreshSourceMenuItems();
                }
            }
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void TrayIcon::BuildMenuOnce() {
    menu_ = CreatePopupMenu();

    AppendMenuW(menu_, MF_STRING, CMD_SET_APP_ID, L"Set Discord Application ID...");

    sourceMenu_ = CreatePopupMenu();
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(sourceMenu_), L"Media Source");

    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu_, MF_STRING, CMD_TOGGLE_BROADCAST, L"Broadcast now playing to Discord");
    AppendMenuW(menu_, MF_STRING, CMD_TOGGLE_TRACK_NUMBER, L"Show track number");

    artMenu_ = CreatePopupMenu();
    AppendMenuW(artMenu_, MF_STRING, CMD_ART_MODE_AUTO, L"Automatic (look up cover art)");
    AppendMenuW(artMenu_, MF_STRING, CMD_ART_MODE_CUSTOM, L"Custom image URL...");
    AppendMenuW(artMenu_, MF_STRING, CMD_ART_MODE_OFF, L"Fallback image only");
    AppendMenuW(artMenu_, MF_STRING, CMD_SET_FALLBACK_KEY, L"Set Fallback Image Key...");
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(artMenu_), L"Album Art");

    pollMenu_ = CreatePopupMenu();
    for (size_t i = 0; i < pollIntervalPresetsMs_.size(); ++i) {
        std::wstring label = std::to_wstring(pollIntervalPresetsMs_[i] / 1000) + L"s";
        AppendMenuW(pollMenu_, MF_STRING, CMD_POLL_INTERVAL_BASE + static_cast<UINT>(i), label.c_str());
    }
    AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(pollMenu_), L"Poll Interval");

    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, CMD_TOGGLE_START_AT_LOGIN, L"Start automatically when you log in");
    AppendMenuW(menu_, MF_STRING, CMD_TOGGLE_TRAY_ENABLED, L"Show tray icon (applies next launch)");

    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, CMD_EXIT, L"Exit");

    // Every submenu above now exists, so it's safe for this to sync
    // checked/radio state (including artMenu_/pollMenu_, which an earlier
    // call couldn't safely touch before they existed).
    RefreshSourceMenuItems();
}

void TrayIcon::RefreshSourceMenuItems() {
    while (GetMenuItemCount(sourceMenu_) > 0) {
        DeleteMenu(sourceMenu_, 0, MF_BYPOSITION);
    }
    for (size_t i = 0; i < mediaSources_.size(); ++i) {
        std::wstring label = platform_windows::WideFromNarrow(mediaSources_[i].displayName);
        AppendMenuW(sourceMenu_, MF_STRING, CMD_MEDIA_SOURCE_BASE + static_cast<UINT>(i), label.c_str());
    }
    SyncMenuState();
}

void TrayIcon::SyncMenuState() {
    CheckMenuItem(menu_, CMD_TOGGLE_BROADCAST, MF_BYCOMMAND | (config_.broadcastEnabled ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu_, CMD_TOGGLE_TRACK_NUMBER, MF_BYCOMMAND | (config_.showTrackNumber ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu_, CMD_TOGGLE_START_AT_LOGIN, MF_BYCOMMAND | (startAtLogin_ ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu_, CMD_TOGGLE_TRAY_ENABLED, MF_BYCOMMAND | (config_.trayEnabled ? MF_CHECKED : MF_UNCHECKED));

    UINT artChecked = config_.artMode == "Custom" ? CMD_ART_MODE_CUSTOM
        : config_.artMode == "Off" ? CMD_ART_MODE_OFF : CMD_ART_MODE_AUTO;
    CheckMenuRadioItem(artMenu_, CMD_ART_MODE_AUTO, CMD_ART_MODE_OFF, artChecked, MF_BYCOMMAND);

    int selectedPollIndex = 0;
    for (size_t i = 0; i < pollIntervalPresetsMs_.size(); ++i) {
        if (pollIntervalPresetsMs_[i] == config_.pollIntervalMs) {
            selectedPollIndex = static_cast<int>(i);
            break;
        }
    }
    CheckMenuRadioItem(pollMenu_, CMD_POLL_INTERVAL_BASE,
        CMD_POLL_INTERVAL_BASE + static_cast<UINT>(pollIntervalPresetsMs_.size()) - 1,
        CMD_POLL_INTERVAL_BASE + static_cast<UINT>(selectedPollIndex), MF_BYCOMMAND);

    int selectedSourceIndex = -1;
    for (size_t i = 0; i < mediaSources_.size(); ++i) {
        if (mediaSources_[i].id == config_.mediaSource) {
            selectedSourceIndex = static_cast<int>(i);
            break;
        }
    }
    if (selectedSourceIndex >= 0) {
        CheckMenuRadioItem(sourceMenu_, CMD_MEDIA_SOURCE_BASE,
            CMD_MEDIA_SOURCE_BASE + static_cast<UINT>(mediaSources_.size()) - 1,
            CMD_MEDIA_SOURCE_BASE + static_cast<UINT>(selectedSourceIndex), MF_BYCOMMAND);
    }
}

void TrayIcon::ShowContextMenu() {
    // Deliberately not awaited - the refresh happens in the background;
    // this call just wakes the one long-lived worker thread, and shows
    // the menu immediately with whatever's already in mediaSources_
    // (stale, or the iTunes-only default on first run) rather than
    // blocking on it. The underlying WinRT GlobalSystemMediaTransport
    // Controls call must never run on this thread synchronously - see
    // StartMediaSourcesRefreshThread()'s comment for why. The *next* time
    // the menu opens it reflects the refresh - same eventually-consistent,
    // cross-thread pattern PostStatusUpdate already uses for the tooltip.
    RequestMediaSourcesRefresh();
    SyncMenuState();

    POINT pt;
    GetCursorPos(&pt);

    // Required so the menu dismisses correctly when the user clicks
    // elsewhere - a well-known Win32 tray-icon quirk, not optional.
    SetForegroundWindow(hwnd_);
    UINT cmd = TrackPopupMenu(menu_, TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, 0, hwnd_, nullptr);
    // Companion half of the SetForegroundWindow fix above.
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (cmd != 0) {
        HandleCommand(cmd);
    }
}

void TrayIcon::HandleCommand(UINT id) {
    if (id == CMD_EXIT) {
        DestroyWindow(hwnd_);
        return;
    }

    if (id == CMD_SET_APP_ID) {
        std::wstring wideId = platform_windows::WideFromNarrow(config_.clientId);
        std::wstring original = wideId;
        if (OnEditApplicationId) {
            OnEditApplicationId(wideId);
        }
        if (wideId != original) {
            config_.clientId = platform_windows::NarrowFromWide(wideId);
            NotifyConfigChanged();
        }
        return;
    }

    if (id == CMD_SET_FALLBACK_KEY) {
        std::wstring wideKey = platform_windows::WideFromNarrow(config_.largeImageKey);
        std::wstring original = wideKey;
        if (OnEditFallbackImageKey) {
            OnEditFallbackImageKey(wideKey);
        }
        if (wideKey != original) {
            config_.largeImageKey = platform_windows::NarrowFromWide(wideKey);
            NotifyConfigChanged();
        }
        return;
    }

    if (id == CMD_ART_MODE_CUSTOM) {
        // Picking "Custom image URL..." both selects Custom mode and
        // immediately prompts for the URL - one action instead of two.
        config_.artMode = "Custom";
        std::wstring wideUrl = platform_windows::WideFromNarrow(config_.customArtUrl);
        if (OnEditCustomArtUrl) {
            OnEditCustomArtUrl(wideUrl);
        }
        config_.customArtUrl = platform_windows::NarrowFromWide(wideUrl);
        NotifyConfigChanged();
        return;
    }

    if (id == CMD_TOGGLE_BROADCAST) {
        config_.broadcastEnabled = !config_.broadcastEnabled;
        NotifyConfigChanged();
        return;
    }

    if (id == CMD_TOGGLE_TRACK_NUMBER) {
        config_.showTrackNumber = !config_.showTrackNumber;
        NotifyConfigChanged();
        return;
    }

    if (id == CMD_TOGGLE_START_AT_LOGIN) {
        startAtLogin_ = !startAtLogin_;
        if (OnStartAtLoginChanged) {
            OnStartAtLoginChanged(startAtLogin_);
        }
        return;
    }

    if (id == CMD_TOGGLE_TRAY_ENABLED) {
        // Cannot apply live - this process can't remove its own tray
        // icon mid-session - main.cpp's OnConfigChanged logs the deferred
        // effect, this class just flips the flag like any other toggle.
        config_.trayEnabled = !config_.trayEnabled;
        NotifyConfigChanged();
        return;
    }

    if (id == CMD_ART_MODE_AUTO) {
        config_.artMode = "Auto";
        NotifyConfigChanged();
        return;
    }

    if (id == CMD_ART_MODE_OFF) {
        config_.artMode = "Off";
        NotifyConfigChanged();
        return;
    }

    if (id >= CMD_MEDIA_SOURCE_BASE && id < CMD_MEDIA_SOURCE_BASE + mediaSources_.size()) {
        config_.mediaSource = mediaSources_[id - CMD_MEDIA_SOURCE_BASE].id;
        NotifyConfigChanged();
        return;
    }

    if (id >= CMD_POLL_INTERVAL_BASE && id < CMD_POLL_INTERVAL_BASE + pollIntervalPresetsMs_.size()) {
        config_.pollIntervalMs = pollIntervalPresetsMs_[id - CMD_POLL_INTERVAL_BASE];
        NotifyConfigChanged();
        return;
    }
}

void TrayIcon::NotifyConfigChanged() {
    if (OnConfigChanged) {
        OnConfigChanged(config_);
    }
}

} // namespace nativeui
