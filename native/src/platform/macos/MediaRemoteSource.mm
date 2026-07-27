#include "MediaRemoteSource.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

#import <Foundation/Foundation.h>

namespace platform_macos {

namespace {

constexpr const char* kSpotifyBundleId = "com.spotify.client";

std::string ResourcePath(const std::string& relative) {
    NSString* base = [[NSBundle mainBundle] resourcePath];
    NSString* full = [base stringByAppendingPathComponent:
        [NSString stringWithFormat:@"mediaremote-adapter/%s", relative.c_str()]];
    return std::string(full.UTF8String);
}

// Runs the bundled adapter with the given arguments and returns stdout
// only on a clean exit (status 0) - anything else (a broken adapter, a
// missing /usr/bin/perl, a future macOS that closes this workaround
// entirely) is treated as "no data", the same "believed correct until
// proven otherwise" posture the rest of this poll loop already takes.
std::optional<std::string> RunAdapter(const std::vector<std::string>& args) {
    @autoreleasepool {
        NSTask* task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/perl"];

        NSMutableArray<NSString*>* nsArgs = [NSMutableArray array];
        [nsArgs addObject:[NSString stringWithUTF8String:ResourcePath("mediaremote-adapter.pl").c_str()]];
        for (const auto& arg : args) {
            [nsArgs addObject:[NSString stringWithUTF8String:arg.c_str()]];
        }
        task.arguments = nsArgs;

        NSPipe* stdoutPipe = [NSPipe pipe];
        task.standardOutput = stdoutPipe;
        task.standardError = [NSPipe pipe]; // discarded, just kept off the real stderr

        NSError* error = nil;
        if (![task launchAndReturnError:&error]) {
            return std::nullopt;
        }

        NSData* data = [stdoutPipe.fileHandleForReading readDataToEndOfFile];
        [task waitUntilExit];

        if (task.terminationStatus != 0) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(data.bytes), data.length);
    }
}

} // namespace

bool MediaRemoteSource::EnsureAdapterWorks() {
    if (_testedAdapter) {
        return _adapterWorks;
    }
    _testedAdapter = true;

    auto result = RunAdapter({
        ResourcePath("MediaRemoteAdapter.framework"),
        ResourcePath("MediaRemoteAdapterTestClient"),
        "test",
    });
    _adapterWorks = result.has_value();
    if (!_adapterWorks) {
        core::Log::Write(
            "[warn] MediaRemote adapter self-test failed - \"Now Playing (any app)\" "
            "will report nothing until FeatherRPC is restarted. This is an unofficial "
            "workaround for Apple's macOS 15.4+ MediaRemote entitlement lockdown and "
            "can break on any macOS update; Music.app's own source is unaffected.");
    }
    return _adapterWorks;
}

std::vector<core::MediaSourceInfo> MediaRemoteSource::GetAvailableSources() {
    return {
        {"Music", "Music.app"},
        {"MediaRemote", "Now Playing (any app)"},
    };
}

std::optional<core::TrackInfo> MediaRemoteSource::GetCurrentTrack() {
    if (!EnsureAdapterWorks()) {
        return std::nullopt;
    }

    auto output = RunAdapter({ResourcePath("MediaRemoteAdapter.framework"), "get", "--no-artwork"});
    if (!output.has_value()) {
        return std::nullopt;
    }

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(*output);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    // A bare `null` line means nothing is currently reporting now-playing
    // info anywhere on the system - not an error, just nothing to show.
    if (json.is_null()) {
        return std::nullopt;
    }

    auto bundleId = json.find("bundleIdentifier");
    if (bundleId != json.end() && bundleId->is_string() && bundleId->get<std::string>() == kSpotifyBundleId) {
        // Spotify has its own official Discord Rich Presence integration -
        // excluded here for the same reason SmtcMediaSource/MprisMediaSource
        // exclude it on Windows/Linux.
        return std::nullopt;
    }

    auto titleIt = json.find("title");
    if (titleIt == json.end() || !titleIt->is_string() || titleIt->get<std::string>().empty()) {
        return std::nullopt;
    }

    core::TrackInfo info;
    info.name = titleIt->get<std::string>();

    if (auto it = json.find("artist"); it != json.end() && it->is_string()) {
        info.artist = it->get<std::string>();
    }
    if (auto it = json.find("album"); it != json.end() && it->is_string()) {
        info.album = it->get<std::string>();
    }
    if (auto it = json.find("duration"); it != json.end() && it->is_number()) {
        info.durationSeconds = it->get<double>();
    }
    if (auto it = json.find("elapsedTime"); it != json.end() && it->is_number()) {
        info.elapsedSeconds = it->get<double>();
    }
    if (auto it = json.find("trackNumber"); it != json.end() && it->is_number_integer()) {
        info.trackNumber = it->get<int>();
    }
    if (auto it = json.find("totalTrackCount"); it != json.end() && it->is_number_integer()) {
        info.trackCount = it->get<int>();
    }

    // The adapter only exposes a "playing" bool, not a three-way
    // playback state - a track with metadata still present but
    // playing == false reads as paused (matching what macOS's own Now
    // Playing widget shows for a paused track), not stopped. Stopped
    // instead comes from the top-level `null` case handled above.
    auto playingIt = json.find("playing");
    bool playing = playingIt != json.end() && playingIt->is_boolean() && playingIt->get<bool>();
    info.state = playing ? core::PlaybackState::Playing : core::PlaybackState::Paused;

    return info;
}

} // namespace platform_macos
