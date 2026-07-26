#pragma once

#include "core/MediaSource.h"

#include <string>
#include <vector>

namespace platform_windows {

// Reads now-playing info via Windows' System Media Transport Controls -
// the same system behind the volume flyout's "now playing" widget. Most
// non-iTunes apps (VLC, browsers, foobar2000, etc.) report playback this
// way, since very few implement a dedicated automation server like iTunes
// does. Spotify is filtered out everywhere here since it has its own
// official Discord integration already - showing it through this app
// would just be redundant.
class SmtcMediaSource : public core::MediaSource {
public:
    explicit SmtcMediaSource(std::string appUserModelId);

    std::optional<core::TrackInfo> GetCurrentTrack() override;

    // Enumerates currently-active SMTC sessions (excluding Spotify) - used
    // to populate the tray's Media Source submenu.
    static std::vector<core::MediaSourceInfo> GetAvailableSources();

private:
    std::string _appUserModelId;
};

} // namespace platform_windows
