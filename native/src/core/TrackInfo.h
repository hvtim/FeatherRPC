#pragma once

#include <string>

namespace core {

enum class PlaybackState { Stopped, Playing, Paused };

struct TrackInfo {
    std::string name;
    std::string artist;
    std::string album;
    double durationSeconds = 0;
    double elapsedSeconds = 0;
    PlaybackState state = PlaybackState::Stopped;
    int trackNumber = 0;
    int trackCount = 0;
};

} // namespace core
