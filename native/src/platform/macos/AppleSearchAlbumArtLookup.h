#pragma once

#include "core/AlbumArtLookup.h"

#include <unordered_map>

namespace platform_macos {

// Looks up cover art via Apple's public iTunes Search API using
// NSURLSession, blocked synchronously via a semaphore since
// PresenceEngine's worker thread expects GetArtworkUrl to return the
// result directly (same blocking-call shape as WinHTTP on Windows). Only
// ever called from that single worker thread, so the cache needs no
// locking.
class AppleSearchAlbumArtLookup : public core::AlbumArtLookup {
public:
    std::optional<std::string> GetArtworkUrl(
        const std::string& artist, const std::string& track, const std::string& album) override;

private:
    std::optional<std::string> Lookup(const std::string& term, const std::string& entity);

    // Tried only when the iTunes lookup above finds no match - MusicBrainz
    // has no direct "search by artist+track, get an image back" endpoint,
    // so this is two requests: find a matching release's MBID, then ask
    // the Cover Art Archive (a companion service, keyed by MBID) whether
    // that release actually has a front cover on file.
    std::optional<std::string> LookupMusicBrainz(
        const std::string& artist, const std::string& track, const std::string& album);
    std::optional<std::string> FindReleaseIdByAlbum(const std::string& artist, const std::string& album);
    std::optional<std::string> FindReleaseIdByRecording(const std::string& artist, const std::string& track);
    std::optional<std::string> CoverArtArchiveFrontUrl(const std::string& releaseId);

    std::unordered_map<std::string, std::optional<std::string>> _cache;
};

} // namespace platform_macos
