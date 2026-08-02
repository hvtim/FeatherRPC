#pragma once

#include "core/MediaSource.h"

#include <string>
#include <vector>

namespace platform_linux {

// One already-selected MPRIS player, addressed by its D-Bus bus name (e.g.
// "org.mpris.MediaPlayer2.vlc") - the Linux equivalent of a Windows SMTC
// app user model id. No single "primary" player the way Windows has
// iTunes; every player is enumerated equally (see GetAvailableSources).
class MprisMediaSource : public core::MediaSource {
public:
    explicit MprisMediaSource(std::string busName);

    std::optional<core::TrackInfo> GetCurrentTrack() override;

    static std::vector<core::MediaSourceInfo> GetAvailableSources();

private:
    std::string _busName;

    // Gates GetCurrentTrack()'s failure logging to state transitions only -
    // it runs once per poll interval (default 2000ms), so logging every
    // miss would flood the log. See MprisMediaSource.cpp.
    bool _lastGetCurrentTrackFailed = false;
};

} // namespace platform_linux
