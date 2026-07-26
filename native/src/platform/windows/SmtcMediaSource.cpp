#include "SmtcMediaSource.h"
#include "ComInit.h"
#include "StringConvert.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

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

} // namespace

SmtcMediaSource::SmtcMediaSource(std::string appUserModelId) : _appUserModelId(std::move(appUserModelId)) {
    EnsureComInitialized();
}

std::vector<core::MediaSourceInfo> SmtcMediaSource::GetAvailableSources() {
    EnsureComInitialized();
    std::vector<core::MediaSourceInfo> result;
    try {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        for (auto const& session : manager.GetSessions()) {
            std::string id = NarrowFromHstring(session.SourceAppUserModelId());
            if (id.empty() || IsSpotify(id)) {
                continue;
            }
            result.push_back({id, PrettifyAppId(id)});
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
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();

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
