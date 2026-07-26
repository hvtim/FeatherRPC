#include "MusicMediaSource.h"
#include "MusicApplication.h"

#import <AppKit/AppKit.h>

namespace platform_macos {

namespace {

bool IsMusicRunning() {
    NSArray<NSRunningApplication*>* apps =
        [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.Music"];
    return apps.count > 0;
}

} // namespace

std::optional<core::TrackInfo> MusicMediaSource::GetCurrentTrack() {
    @autoreleasepool {
        // SBApplication activates (launches) Music.app on first property
        // access, same as iTunes.Application's COM activation on Windows -
        // guard against that so this app doesn't pop Music open on its own
        // while idling at login.
        if (!IsMusicRunning()) {
            return std::nullopt;
        }

        id<MusicApplication> music =
            (id<MusicApplication>)[SBApplication applicationWithBundleIdentifier:@"com.apple.Music"];
        if (!music) {
            return std::nullopt;
        }

        @try {
            core::TrackInfo info;
            switch (music.playerState) {
                case MusicEPlSPlaying:
                    info.state = core::PlaybackState::Playing;
                    break;
                case MusicEPlSPaused:
                    info.state = core::PlaybackState::Paused;
                    break;
                default:
                    info.state = core::PlaybackState::Stopped;
                    break;
            }

            if (info.state == core::PlaybackState::Stopped) {
                return info;
            }

            id<MusicTrack> track = music.currentTrack;
            if (!track) {
                return core::TrackInfo{};
            }

            info.name = track.name ? std::string(track.name.UTF8String) : std::string();
            info.artist = track.artist ? std::string(track.artist.UTF8String) : std::string();
            info.album = track.album ? std::string(track.album.UTF8String) : std::string();
            info.durationSeconds = track.duration;
            info.trackNumber = static_cast<int>(track.trackNumber);
            info.trackCount = static_cast<int>(track.trackCount);
            info.elapsedSeconds = music.playerPosition;
            return info;
        } @catch (NSException* exception) {
            // Music.app's scripting interface can throw if a property is
            // unavailable mid-transition (e.g. between tracks) - treat as
            // "nothing meaningful right now" rather than crashing the poll.
            return std::nullopt;
        }
    }
}

} // namespace platform_macos
