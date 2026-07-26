#pragma once

#include "core/AlbumArtLookup.h"

#include <unordered_map>

namespace platform_linux {

// Looks up official cover art via Apple's public iTunes Search API (no
// auth needed), same source as the Windows WinHTTP implementation. Only
// ever called from PresenceEngine's single worker thread, so the cache
// needs no locking.
class CurlAlbumArtLookup : public core::AlbumArtLookup {
public:
    std::optional<std::string> GetArtworkUrl(
        const std::string& artist, const std::string& track, const std::string& album) override;

private:
    std::optional<std::string> Lookup(const std::string& term, const std::string& entity);

    std::unordered_map<std::string, std::optional<std::string>> _cache;
};

} // namespace platform_linux
