#include "SmtcMediaSource.h"
#include "ComInit.h"
#include "StringConvert.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <propsys.h>
#include <propvarutil.h>

#include <initguid.h>
#include <propkey.h>

#include <winver.h>

// Declared in shellapi.h/shlwapi.h, but those headers conflict with the
// C++/WinRT headers above when both land in one translation unit
// (redefinition errors deep in their own unrelated declarations) - both are
// plain Shell32/Shlwapi exports, so just forward-declare them instead of
// pulling in the whole header for one function each.
extern "C" __declspec(dllimport) HRESULT __stdcall SHGetPropertyStoreForWindow(
    HWND hwnd, REFIID riid, void** ppv);
extern "C" __declspec(dllimport) HRESULT __stdcall SHLoadIndirectString(
    PCWSTR pszSource, PWSTR pszOutBuf, UINT cchOutBuf, void** ppvReserved);

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace winrt::Windows::Media::Control;

namespace platform_windows {

namespace {

constexpr const char* kSpotifyAppId = "Spotify.exe";

bool IsSpotify(const std::string& appId) {
    return _stricmp(appId.c_str(), kSpotifyAppId) == 0;
}

std::string NarrowFromHstring(winrt::hstring const& value) {
    return NarrowFromWide(std::wstring_view(value.c_str(), value.size()));
}

std::string PrettifyAppId(const std::string& appId) {
    std::string name = appId;
    const std::string suffix = ".exe";
    if (name.size() >= suffix.size()) {
        std::string tail = name.substr(name.size() - suffix.size());
        if (_stricmp(tail.c_str(), suffix.c_str()) == 0) {
            name.resize(name.size() - suffix.size());
        }
    }
    if (!name.empty()) {
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    }
    return name;
}

// Most apps' main window title ends in " - AppName" / " — AppName" (browsers,
// editors, etc. all follow this convention for taskbar/Alt+Tab clarity) -
// take the trailing segment rather than the whole title, which would
// otherwise be the current page/document, not the app.
std::string AppNameFromWindowTitle(const std::string& title) {
    for (const char* sep : {" \xe2\x80\x94 ", " - "}) { // "\xe2\x80\x94" = em dash (UTF-8)
        auto pos = title.rfind(sep);
        if (pos != std::string::npos) {
            return title.substr(pos + std::strlen(sep));
        }
    }
    return title;
}

struct EnumContext {
    const wchar_t* targetAumid;
    std::wstring foundTitle;
    std::wstring relaunchDisplayNameResource;
    DWORD ownerProcessId = 0;
    bool matched = false; // true once any window with this AUMID has been seen, even titleless
};

BOOL CALLBACK FindWindowByAumidProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store))) || !store) {
        return TRUE;
    }

    PROPVARIANT prop;
    PropVariantInit(&prop);
    bool matched = false;
    if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID, &prop))) {
        wchar_t buffer[512] = {};
        if (SUCCEEDED(PropVariantToString(prop, buffer, ARRAYSIZE(buffer))) &&
            _wcsicmp(buffer, ctx->targetAumid) == 0) {
            matched = true;
        }
    }
    PropVariantClear(&prop);

    // Only capture resolver data (RelaunchDisplayNameResource, owning
    // process) from the first AUMID-matching window seen - regardless of
    // whether it turns out to have a usable title below. Both of these are
    // process-scoped, not window-scoped, so it doesn't matter which of the
    // process's windows this happens to be.
    if (matched && !ctx->matched) {
        PROPVARIANT nameProp;
        PropVariantInit(&nameProp);
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_RelaunchDisplayNameResource, &nameProp))) {
            wchar_t nameBuffer[512] = {};
            if (SUCCEEDED(PropVariantToString(nameProp, nameBuffer, ARRAYSIZE(nameBuffer)))) {
                ctx->relaunchDisplayNameResource = nameBuffer;
            }
        }
        PropVariantClear(&nameProp);

        GetWindowThreadProcessId(hwnd, &ctx->ownerProcessId);
        ctx->matched = true;
    }
    store->Release();

    if (!matched) {
        return TRUE;
    }

    int length = GetWindowTextLengthW(hwnd);
    if (length > 0) {
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(hwnd, title.data(), length + 1);
        title.resize(static_cast<size_t>(length));
        if (!title.empty()) {
            ctx->foundTitle = std::move(title);
            return FALSE; // found it, stop enumerating
        }
    }
    return TRUE;
}

// PKEY_AppUserModel_RelaunchDisplayNameResource may be a plain string or an
// indirect resource reference ("@app.exe,-123") - SHLoadIndirectString
// handles both (a literal string passes through unchanged), so there's no
// need to detect which form it is first.
std::string ResolveIndirectString(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    wchar_t buffer[512] = {};
    if (FAILED(SHLoadIndirectString(value.c_str(), buffer, ARRAYSIZE(buffer), nullptr))) {
        return {};
    }
    return NarrowFromWide(buffer);
}

// Reads the owning process's own embedded version info (what Explorer/Task
// Manager show as "Description") - authoritative, and unlike a window
// title, doesn't depend on *which* of the process's windows got matched by
// AUMID, since it's a property of the process/exe, not any one window.
std::string DisplayNameFromProcessVersionInfo(DWORD processId) {
    if (processId == 0) {
        return {};
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return {};
    }

    wchar_t path[MAX_PATH] = {};
    DWORD pathLen = ARRAYSIZE(path);
    bool ok = QueryFullProcessImageNameW(process, 0, path, &pathLen);
    CloseHandle(process);
    if (!ok) {
        return {};
    }

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) {
        return {};
    }

    std::vector<BYTE> buffer(size);
    if (!GetFileVersionInfoW(path, handle, size, buffer.data())) {
        return {};
    }

    struct LangCodepage { WORD language; WORD codepage; };
    LangCodepage* translations = nullptr;
    UINT translationsBytes = 0;
    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation",
                         reinterpret_cast<void**>(&translations), &translationsBytes) ||
        !translations || translationsBytes < sizeof(LangCodepage)) {
        return {};
    }

    auto queryField = [&](const wchar_t* fieldName) -> std::string {
        wchar_t subBlock[64];
        swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%s",
                   translations[0].language, translations[0].codepage, fieldName);
        void* value = nullptr;
        UINT valueLenChars = 0;
        if (!VerQueryValueW(buffer.data(), subBlock, &value, &valueLenChars) || !value || valueLenChars == 0) {
            return {};
        }
        // valueLenChars includes the terminating null per VerQueryValueW's docs - trim it.
        size_t len = valueLenChars;
        auto* text = static_cast<const wchar_t*>(value);
        while (len > 0 && text[len - 1] == L'\0') {
            --len;
        }
        return NarrowFromWide(std::wstring_view(text, len));
    };

    std::string description = queryField(L"FileDescription");
    if (!description.empty()) {
        return description;
    }
    return queryField(L"ProductName");
}

// Resolution order, always attempted regardless of what the AUMID string
// looks like (an earlier version gated this behind an "is this AUMID all
// hex" heuristic - dropped because it's wrong for any opaque AUMID that
// doesn't happen to be pure hex, e.g. Vivaldi's "Vivaldi.<random>" shape):
//   1. PKEY_AppUserModel_RelaunchDisplayNameResource - the actual property
//      apps set to declare their own Start-menu/taskbar display name.
//   2. The owning process's own embedded FileDescription/ProductName.
//   3. The window-title heuristic (AppNameFromWindowTitle) - kept as a
//      lower-priority fallback for apps that set neither property above.
//   4. PrettifyAppId(appId) on the raw AUMID - final fallback.
std::string ResolveDisplayNameForAppId(const std::string& appId) {
    std::wstring wideAumid = WideFromNarrow(appId);
    EnumContext ctx{wideAumid.c_str(), {}, {}, 0, false};
    EnumWindows(FindWindowByAumidProc, reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.matched) {
        return PrettifyAppId(appId); // nothing currently declares this AUMID at all
    }

    std::string name = ResolveIndirectString(ctx.relaunchDisplayNameResource);
    if (!name.empty()) {
        return name;
    }

    name = DisplayNameFromProcessVersionInfo(ctx.ownerProcessId);
    if (!name.empty()) {
        return name;
    }

    if (!ctx.foundTitle.empty()) {
        name = AppNameFromWindowTitle(NarrowFromWide(ctx.foundTitle));
        if (!name.empty()) {
            return name;
        }
    }

    return PrettifyAppId(appId);
}

// Microsoft's own guidance for GlobalSystemMediaTransportControlsSessionManager
// is to request it once and reuse it, not call RequestAsync() fresh on
// every poll - confirmed by isolated testing (issue #57 follow-up) that
// re-requesting it every call leaks ~5-6KB of process-private memory per
// call, steadily and without bound, with zero UI/menu code involved (a
// tight loop calling only this, nothing else, reproduced it exactly).
// thread_local rather than one shared instance, so this doesn't need to
// reason about cross-thread call safety for the cached object - each
// thread that touches SMTC already does its own COM apartment init
// (EnsureComInitialized()) and only needs to acquire this once, ever.
GlobalSystemMediaTransportControlsSessionManager GetSessionManager() {
    thread_local GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    if (!manager) {
        manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    }
    return manager;
}

// GetAvailableSources() runs on a dedicated background thread every time
// the right-click menu is opened (see TrayIcon's media-source refresh
// thread) - on a desktop with many open windows, EnumWindows + a
// property-store/process/version-info query per window still adds up
// across repeated calls. Caching by AUMID means that cost is paid at most
// once per session per process lifetime instead of on every refresh -
// resolution is always attempted (no shape-based pre-filter on the AUMID),
// since the cache already amortizes the one-time cost per app.
std::string DisplayNameForAppId(const std::string& appId) {
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(appId);
    if (it != cache.end()) {
        return it->second;
    }
    std::string name = ResolveDisplayNameForAppId(appId);
    cache.emplace(appId, name);
    return name;
}

// Truncated so a long browser-tab title can't blow out the Media Source
// submenu's width - truncates in UTF-16 (wchar_t) space, before narrowing
// to UTF-8, so this can never split a multi-byte UTF-8 sequence.
constexpr size_t kMaxTitleLengthForMenu = 40;

std::string TrackTitleForMenu(GlobalSystemMediaTransportControlsSession const& session) {
    try {
        auto props = session.TryGetMediaPropertiesAsync().get();
        auto hTitle = props.Title();
        std::wstring_view title(hTitle.c_str(), hTitle.size());
        if (title.size() > kMaxTitleLengthForMenu) {
            return NarrowFromWide(title.substr(0, kMaxTitleLengthForMenu)) + "...";
        }
        return NarrowFromWide(title);
    } catch (...) {
        return {}; // no track properties available right now - show just the app name
    }
}

} // namespace

SmtcMediaSource::SmtcMediaSource(std::string appUserModelId) : _appUserModelId(std::move(appUserModelId)) {
    EnsureComInitialized();
}

std::vector<core::MediaSourceInfo> SmtcMediaSource::GetAvailableSources() {
    EnsureComInitialized();
    std::vector<core::MediaSourceInfo> result;
    try {
        auto manager = GetSessionManager();
        for (auto const& session : manager.GetSessions()) {
            std::string id = NarrowFromHstring(session.SourceAppUserModelId());
            if (id.empty() || IsSpotify(id)) {
                continue;
            }
            std::string label = DisplayNameForAppId(id);
            std::string title = TrackTitleForMenu(session);
            if (!title.empty()) {
                label += " \xe2\x80\x94 " + title; // em dash, matches AppNameFromWindowTitle's separator style
            }
            result.push_back({id, label});
        }
    } catch (...) {
        // SMTC unavailable (very old Windows build, or the API threw) -
        // callers just see an empty list and fall back to iTunes-only.
    }
    return result;
}

std::optional<core::TrackInfo> SmtcMediaSource::GetCurrentTrack() {
    EnsureComInitialized();
    if (IsSpotify(_appUserModelId)) {
        return std::nullopt;
    }

    try {
        auto manager = GetSessionManager();

        GlobalSystemMediaTransportControlsSession target{nullptr};
        for (auto const& session : manager.GetSessions()) {
            std::string id = NarrowFromHstring(session.SourceAppUserModelId());
            if (_stricmp(id.c_str(), _appUserModelId.c_str()) == 0) {
                target = session;
                break;
            }
        }
        if (!target) {
            return std::nullopt;
        }

        auto playback = target.GetPlaybackInfo();
        core::PlaybackState state;
        switch (playback.PlaybackStatus()) {
            case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
                state = core::PlaybackState::Playing;
                break;
            case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
                state = core::PlaybackState::Paused;
                break;
            default:
                state = core::PlaybackState::Stopped;
                break;
        }

        if (state == core::PlaybackState::Stopped) {
            return core::TrackInfo{};
        }

        auto props = target.TryGetMediaPropertiesAsync().get();
        auto timeline = target.GetTimelineProperties();

        double duration = std::chrono::duration<double>(timeline.EndTime() - timeline.StartTime()).count();
        double position = std::chrono::duration<double>(timeline.Position()).count();

        // SMTC only reports position at discrete update points, not a
        // continuous stream - while playing, extrapolate from how long
        // it's been since that last update rather than showing a position
        // that's stuck between polls. Only do this when duration is
        // known, so it can be clamped: some sources (weak SMTC
        // implementations, some browsers) never populate EndTime/StartTime,
        // and without a duration to clamp against, extrapolating forever
        // means a source that goes quiet while still reporting "Playing"
        // drifts further from reality the longer it goes unrefreshed.
        double elapsed;
        if (duration > 0 && state == core::PlaybackState::Playing) {
            double secondsSinceUpdate = std::chrono::duration<double>(winrt::clock::now() - timeline.LastUpdatedTime()).count();
            elapsed = position + secondsSinceUpdate;
            elapsed = std::max(0.0, std::min(elapsed, duration));
        } else {
            elapsed = std::max(0.0, position);
        }

        core::TrackInfo info;
        info.name = NarrowFromHstring(props.Title());
        info.artist = NarrowFromHstring(props.Artist());
        info.album = NarrowFromHstring(props.AlbumTitle());
        info.durationSeconds = duration;
        info.elapsedSeconds = elapsed;
        info.state = state;
        return info;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace platform_windows
