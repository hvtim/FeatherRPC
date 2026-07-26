#pragma once

#include "core/MediaSource.h"

namespace platform_macos {

// Talks to Music.app via Scripting Bridge - confirmed in the plan's
// research as faster/more idiomatic than NSAppleScript for this. Music.app
// only, no MediaRemote/other-apps support - see the plan's Phase 3 scope
// note on Apple's macOS 15.4 MediaRemote entitlement lockdown.
class MusicMediaSource : public core::MediaSource {
public:
    std::optional<core::TrackInfo> GetCurrentTrack() override;
};

} // namespace platform_macos
