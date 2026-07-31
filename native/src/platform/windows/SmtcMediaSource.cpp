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

// Declared in shellapi.h, but that header conflicts with the C++/WinRT
// headers above when both land in one translation unit (redefinition
// errors deep in shellapi.h's own unrelated declarations) - it's a plain
// Shell32 export, so just forward-declare it instead of pulling in the
// whole header for this one function.
extern "C" __declspec(dllimport) HRESULT __stdcall SHGetPropertyStoreForWindow(
    HWND hwnd, REFIID riid, void** ppv);

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <unordered_map>

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

// Packaged (UWP) AUMIDs look like "Publisher.App_8wekyb3d8bbwe!AppId" -
// always contain a letter outside a-f or a separator, so PrettifyAppId
// already reads fine. Some Win32 apps (observed with LibreWolf) instead
// register a per-install AUMID that's just a random hex string with no
// human meaning at all - that's what this heuristic catches, so those
// fall through to the window-title lookup below instead.
bool LooksLikeOpaqueId(const std::string& appId) {
    std::string name = appId;
    const std::string suffix = ".exe";
    if (name.size() >= suffix.size() &&
        _stricmp(name.substr(name.size() - suffix.size()).c_str(), suffix.c_str()) == 0) {
        name.resize(name.size() - suffix.size());
    }
    if (name.size() < 8) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
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

// Falls back to PrettifyAppId(appId) if no matching window is found (or
// its title is empty) - e.g. the session's app has no visible top-level
// window at all right now.
std::string ResolveDisplayNameForAppId(const std::string& appId) {
    std::wstring wideAumid = WideFromNarrow(appId);
    EnumContext ctx{wideAumid.c_str(), {}};
    EnumWindows(FindWindowByAumidProc, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.foundTitle.empty()) {
        return PrettifyAppId(appId);
    }
    std::string name = AppNameFromWindowTitle(NarrowFromWide(ctx.foundTitle));
    return name.empty() ? PrettifyAppId(appId) : name;
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
// property-store query per window still adds up across repeated calls.
// Caching by AUMID means that cost is paid at most once per session per
// process lifetime instead of on every refresh.
std::string DisplayNameForAppId(const std::string& appId) {
    if (!LooksLikeOpaqueId(appId)) {
        return PrettifyAppId(appId);
    }

    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(appId);
    if (it != cache.end()) {
        return it->second;
    }
    std::string name = ResolveDisplayNameForAppId(appId);
    cache.emplace(appId, name);
    return name;
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
            result.push_back({id, DisplayNameForAppId(id)});
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
