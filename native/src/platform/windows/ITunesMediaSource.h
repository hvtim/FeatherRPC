#pragma once

#include "core/MediaSource.h"

#include <windows.h>

namespace platform_windows {

// Talks to iTunes via late-bound COM (CLSIDFromProgID + IDispatch) so no
// type library reference is needed at build time - just iTunes installed
// at runtime.
class ITunesMediaSource : public core::MediaSource {
public:
    ITunesMediaSource();

    std::optional<core::TrackInfo> GetCurrentTrack() override;

private:
    CLSID _clsid{};
    bool _clsidValid = false;
};

} // namespace platform_windows
