#pragma once

#include "TrackInfo.h"

#include <optional>
#include <string>

namespace core {

struct MediaSourceInfo {
    std::string id;
    std::string displayName;

    bool operator==(const MediaSourceInfo& other) const {
        return id == other.id && displayName == other.displayName;
    }
};

// One already-selected source (iTunes, or one specific SMTC app) that
// PresenceEngine polls without knowing which platform/backend it is - see
// the plan's "Media Source Per Platform" section.
class MediaSource {
public:
    virtual ~MediaSource() = default;

    // std::nullopt means nothing meaningful is playing (engine clears the
    // Discord activity); otherwise the current track/state.
    virtual std::optional<TrackInfo> GetCurrentTrack() = 0;
};

} // namespace core
