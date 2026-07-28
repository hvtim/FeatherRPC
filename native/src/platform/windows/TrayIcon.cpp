#include "TrayIcon.h"
#include "DarkMode.h"
#include "StringConvert.h"
#include "resource.h"

#include <commctrl.h>

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
    if (hwnd_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        DestroyWindow(hwnd_);
    }
    if (icon_) {
        DestroyIcon(icon_);
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
    // decoding the largest frame and scaling it down.
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (FAILED(LoadIconWithScaleDown(hInstance_, MAKEINTRESOURCEW(IDI_APP_ICON), cx, cy, &icon_))) {
        icon_ = nullptr;
    }
    nid_.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid_.szTip, tooltip.c_str(), _TRUNCATE);

    return Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
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
    nid_.uFlags |= NIF_INFO;
    wcscpy_s(nid_.szInfoTitle, L"FeatherRPC");
    wcscpy_s(nid_.szInfo, L"FeatherRPC is running. Right-click the tray icon to set your Discord Application ID.");
    nid_.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
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
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

HMENU TrayIcon::BuildMenu() {
    HMENU menu = CreatePopupMenu();

    AppendMenuW(menu, MF_STRING, CMD_SET_APP_ID, L"Set Discord Application ID...");

    HMENU sourceMenu = CreatePopupMenu();
    int selectedSourceIndex = -1;
    for (size_t i = 0; i < mediaSources_.size(); ++i) {
        std::wstring label = platform_windows::WideFromNarrow(mediaSources_[i].displayName);
        AppendMenuW(sourceMenu, MF_STRING, CMD_MEDIA_SOURCE_BASE + static_cast<UINT>(i), label.c_str());
        if (mediaSources_[i].id == config_.mediaSource) {
            selectedSourceIndex = static_cast<int>(i);
        }
    }
    if (selectedSourceIndex >= 0) {
        CheckMenuRadioItem(sourceMenu, CMD_MEDIA_SOURCE_BASE,
            CMD_MEDIA_SOURCE_BASE + static_cast<UINT>(mediaSources_.size()) - 1,
            CMD_MEDIA_SOURCE_BASE + static_cast<UINT>(selectedSourceIndex), MF_BYCOMMAND);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sourceMenu), L"Media Source");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | (config_.broadcastEnabled ? MF_CHECKED : MF_UNCHECKED),
        CMD_TOGGLE_BROADCAST, L"Broadcast now playing to Discord");
    AppendMenuW(menu, MF_STRING | (config_.showTrackNumber ? MF_CHECKED : MF_UNCHECKED),
        CMD_TOGGLE_TRACK_NUMBER, L"Show track number");

    HMENU artMenu = CreatePopupMenu();
    AppendMenuW(artMenu, MF_STRING, CMD_ART_MODE_AUTO, L"Automatic (look up cover art)");
    AppendMenuW(artMenu, MF_STRING, CMD_ART_MODE_CUSTOM, L"Custom image URL...");
    AppendMenuW(artMenu, MF_STRING, CMD_ART_MODE_OFF, L"Fallback image only");
    UINT artChecked = config_.artMode == "Custom" ? CMD_ART_MODE_CUSTOM
        : config_.artMode == "Off" ? CMD_ART_MODE_OFF : CMD_ART_MODE_AUTO;
    CheckMenuRadioItem(artMenu, CMD_ART_MODE_AUTO, CMD_ART_MODE_OFF, artChecked, MF_BYCOMMAND);
    AppendMenuW(artMenu, MF_STRING, CMD_SET_FALLBACK_KEY, L"Set Fallback Image Key...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(artMenu), L"Album Art");

    HMENU pollMenu = CreatePopupMenu();
    int selectedPollIndex = 0;
    for (size_t i = 0; i < pollIntervalPresetsMs_.size(); ++i) {
        std::wstring label = std::to_wstring(pollIntervalPresetsMs_[i] / 1000) + L"s";
        AppendMenuW(pollMenu, MF_STRING, CMD_POLL_INTERVAL_BASE + static_cast<UINT>(i), label.c_str());
        if (pollIntervalPresetsMs_[i] == config_.pollIntervalMs) {
            selectedPollIndex = static_cast<int>(i);
        }
    }
    CheckMenuRadioItem(pollMenu, CMD_POLL_INTERVAL_BASE,
        CMD_POLL_INTERVAL_BASE + static_cast<UINT>(pollIntervalPresetsMs_.size()) - 1,
        CMD_POLL_INTERVAL_BASE + static_cast<UINT>(selectedPollIndex), MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pollMenu), L"Poll Interval");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (startAtLogin_ ? MF_CHECKED : MF_UNCHECKED),
        CMD_TOGGLE_START_AT_LOGIN, L"Start automatically when you log in");
    AppendMenuW(menu, MF_STRING | (config_.trayEnabled ? MF_CHECKED : MF_UNCHECKED),
        CMD_TOGGLE_TRAY_ENABLED, L"Show tray icon (applies next launch)");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"Exit");

    return menu;
}

void TrayIcon::ShowContextMenu() {
    if (OnRefreshMediaSources) {
        auto sources = OnRefreshMediaSources();
        mediaSources_.clear();
        mediaSources_.push_back({"iTunes", "iTunes"});
        for (auto& source : sources) {
            mediaSources_.push_back(source);
        }
    }

    HMENU menu = BuildMenu();

    POINT pt;
    GetCursorPos(&pt);

    // Required so the menu dismisses correctly when the user clicks
    // elsewhere - a well-known Win32 tray-icon quirk, not optional.
    SetForegroundWindow(hwnd_);
    UINT cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
        pt.x, pt.y, 0, hwnd_, nullptr);
    // Companion half of the SetForegroundWindow fix above.
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    DestroyMenu(menu);

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
