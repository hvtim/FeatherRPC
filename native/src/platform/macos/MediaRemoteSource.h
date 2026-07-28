#pragma once

#include "core/MediaSource.h"

#include <optional>
#include <string>
#include <vector>

namespace platform_macos {

// Reads system-wide "Now Playing" info (any app, not just Music.app) via
// a bundled Perl script + helper framework from the mediaremote-adapter
// project (see native/CMakeLists.txt's FetchContent_Declare). This exists
// because Apple locked MediaRemote.framework itself - the only API that
// can read *other* apps' now-playing state - behind an Apple-only
// entitlement starting in macOS 15.4; the bundled script works around
// that by shelling out to /usr/bin/perl, one of the few processes Apple
// still grants the entitlement to. This is an unofficial workaround, not
// a public API - see docs/KnownIssues.md for the risk this carries.
//
// Music.app keeps its own separate MusicMediaSource (Scripting Bridge) -
// this is an additional, separately-selectable source, not a replacement,
// so Music-only users aren't exposed to this workaround at all.
class MediaRemoteSource : public core::MediaSource {
public:
    // appFilter empty (default) means "any app" - the original catch-all
    // behavior. A non-empty bundle identifier (e.g. "com.google.Chrome")
    // narrows this to only report tracks from that specific app, treating
    // every other app's now-playing state as "nothing playing" - see
    // GetCurrentBundleId() below for how a caller discovers the string to
    // pass here.
    explicit MediaRemoteSource(std::string appFilter = "");

    std::optional<core::TrackInfo> GetCurrentTrack() override;

    // Fixed 2-entry list (Music.app's own source, plus this one) - unlike
    // Windows/Linux, MediaRemote has no concept of "list every active
    // app," only "whichever one is currently the system's Now Playing
    // session," so there's nothing to dynamically enumerate.
    static std::vector<core::MediaSourceInfo> GetAvailableSources();

    // One-shot query for whichever app is *right now* the system's active
    // Now Playing session, regardless of any configured appFilter -
    // there's no way to enumerate every app that could report now-playing
    // info (see class comment on GetAvailableSources), so this is how a
    // caller (the CLI's `mediasource current`) discovers the exact bundle
    // identifier string to filter to. std::nullopt if nothing is playing
    // or the adapter itself doesn't work.
    static std::optional<std::string> GetCurrentBundleId();

private:
    std::string _appFilter;

    // Lazily run once (on first GetCurrentTrack(), not per-poll) - if the
    // adapter is broken (a future macOS tightening the entitlement
    // further, or /usr/bin/perl missing), there's no point re-spawning a
    // doomed process every 2 seconds forever.
    bool _testedAdapter = false;
    bool _adapterWorks = false;

    bool EnsureAdapterWorks();
};

} // namespace platform_macos
