#pragma once

#include <optional>
#include <string>

namespace core {

// Looks up official cover art (e.g. via Apple's public iTunes Search API)
// so Discord has a real internet-reachable image URL per track - it can't
// render artwork bytes pulled straight out of a local media library. An
// interface because the HTTP client is inherently platform-specific
// (WinHTTP on Windows; a portable C++ stdlib HTTP client doesn't exist).
class AlbumArtLookup {
public:
    virtual ~AlbumArtLookup() = default;

    virtual std::optional<std::string> GetArtworkUrl(
        const std::string& artist, const std::string& track, const std::string& album) = 0;
};

} // namespace core
