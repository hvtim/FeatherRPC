#include "AppleSearchAlbumArtLookup.h"

#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

#include <nlohmann/json.hpp>

namespace platform_macos {

namespace {

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

// Shared by the iTunes, MusicBrainz, and Cover Art Archive lookups below -
// same timeout/user-agent, same "2xx or nothing" success rule. Returns the
// response body only on a 2xx status; anything else (404, timeout,
// malformed response) is treated as "no data", not an error to propagate.
std::optional<std::string> PerformGet(const std::string& urlString) {
    @autoreleasepool {
        NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:urlString.c_str()]];
        if (!url) {
            return std::nullopt;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        // Matches the 5-second timeout used by WinHTTP on Windows / libcurl
        // on Linux for this same lookup.
        request.timeoutInterval = 5.0;
        // MusicBrainz's API policy requires a descriptive user-agent
        // identifying the application, or requests get rate-limited more
        // aggressively.
        [request setValue:@"FeatherRPC/0.1 ( https://github.com/hvtim/FeatherRPC )" forHTTPHeaderField:@"User-Agent"];

        __block std::optional<std::string> result;
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);

        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                  if (!error && data) {
                      NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
                      if (http.statusCode >= 200 && http.statusCode < 300) {
                          result = std::string(static_cast<const char*>(data.bytes), data.length);
                      }
                  }
                  dispatch_semaphore_signal(sema);
              }];
        [task resume];

        // Blocks this call - always PresenceEngine's own worker thread,
        // never the main/UI thread - until the completion handler above
        // fires. The extra second past the request's own 5s timeout leaves
        // room for the completion handler itself to run.
        dispatch_semaphore_wait(sema, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(6.0 * NSEC_PER_SEC)));

        return result;
    }
}

} // namespace

std::optional<std::string> AppleSearchAlbumArtLookup::GetArtworkUrl(
    const std::string& artist, const std::string& track, const std::string& album) {
    std::string key = artist + "|" + track + "|" + album;
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }

    // Prefer an album-title search when we have one - matches iTunes/Music's
    // own tagging and avoids picking up a different single/compilation's
    // cover art. Every track on an album shares the same cover art anyway.
    std::optional<std::string> url;
    if (!album.empty()) {
        url = Lookup(artist + " " + album, "album");
    }
    if (!url.has_value()) {
        url = Lookup(artist + " " + track, "song");
    }
    if (!url.has_value()) {
        url = LookupMusicBrainz(artist, track, album);
    }

    _cache[key] = url;
    return url;
}

namespace {
// NSString's own percent-encoding, wrapped for callers that only have a
// std::string query term/query string to encode.
std::string UrlEncode(const std::string& value) {
    @autoreleasepool {
        NSString* ns = [NSString stringWithUTF8String:value.c_str()];
        if (!ns) {
            return "";
        }
        NSCharacterSet* allowed = [NSCharacterSet URLQueryAllowedCharacterSet];
        NSString* encoded = [ns stringByAddingPercentEncodingWithAllowedCharacters:allowed];
        return encoded ? std::string([encoded UTF8String]) : "";
    }
}
} // namespace

std::optional<std::string> AppleSearchAlbumArtLookup::Lookup(const std::string& term, const std::string& entity) {
    std::string urlString = "https://itunes.apple.com/search?term=" + UrlEncode(Trim(term)) + "&entity=" + entity + "&limit=1";
    auto body = PerformGet(urlString);
    if (!body) {
        return std::nullopt;
    }

    std::optional<std::string> result;
    try {
        auto json = nlohmann::json::parse(*body);
        auto resultsIt = json.find("results");
        if (resultsIt != json.end() && resultsIt->is_array() && !resultsIt->empty()) {
            auto artIt = (*resultsIt)[0].find("artworkUrl100");
            if (artIt != (*resultsIt)[0].end() && artIt->is_string()) {
                std::string art = artIt->get<std::string>();
                if (!art.empty()) {
                    // Apple's CDN accepts an arbitrary size baked into
                    // the filename - ask for bigger than the default
                    // 100x100 thumbnail.
                    ReplaceAll(art, "100x100bb", "512x512bb");
                    result = art;
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Leave result empty - malformed/unexpected body.
    }
    return result;
}

std::optional<std::string> AppleSearchAlbumArtLookup::LookupMusicBrainz(
    const std::string& artist, const std::string& track, const std::string& album) {
    std::optional<std::string> releaseId;
    if (!album.empty()) {
        releaseId = FindReleaseIdByAlbum(artist, album);
    }
    if (!releaseId.has_value() && !track.empty()) {
        releaseId = FindReleaseIdByRecording(artist, track);
    }
    if (!releaseId.has_value()) {
        return std::nullopt;
    }
    return CoverArtArchiveFrontUrl(*releaseId);
}

std::optional<std::string> AppleSearchAlbumArtLookup::FindReleaseIdByAlbum(
    const std::string& artist, const std::string& album) {
    std::string query = "artist:\"" + Trim(artist) + "\" AND release:\"" + Trim(album) + "\"";
    auto body = PerformGet("https://musicbrainz.org/ws/2/release/?query=" + UrlEncode(query) + "&fmt=json&limit=1");
    if (!body) {
        return std::nullopt;
    }
    try {
        auto json = nlohmann::json::parse(*body);
        auto releasesIt = json.find("releases");
        if (releasesIt != json.end() && releasesIt->is_array() && !releasesIt->empty()) {
            auto idIt = (*releasesIt)[0].find("id");
            if (idIt != (*releasesIt)[0].end() && idIt->is_string()) {
                return idIt->get<std::string>();
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Leave empty - malformed/unexpected response body.
    }
    return std::nullopt;
}

std::optional<std::string> AppleSearchAlbumArtLookup::FindReleaseIdByRecording(
    const std::string& artist, const std::string& track) {
    std::string query = "artist:\"" + Trim(artist) + "\" AND recording:\"" + Trim(track) + "\"";
    auto body = PerformGet("https://musicbrainz.org/ws/2/recording/?query=" + UrlEncode(query) + "&fmt=json&limit=1");
    if (!body) {
        return std::nullopt;
    }
    try {
        auto json = nlohmann::json::parse(*body);
        auto recordingsIt = json.find("recordings");
        if (recordingsIt == json.end() || !recordingsIt->is_array() || recordingsIt->empty()) {
            return std::nullopt;
        }
        // A recording search result includes its associated releases
        // directly (no separate inc=releases request needed) - take the
        // first one, same "good enough" choice iTunes/MPRIS tagging makes
        // when a track could belong to more than one release.
        auto releasesIt = (*recordingsIt)[0].find("releases");
        if (releasesIt != (*recordingsIt)[0].end() && releasesIt->is_array() && !releasesIt->empty()) {
            auto idIt = (*releasesIt)[0].find("id");
            if (idIt != (*releasesIt)[0].end() && idIt->is_string()) {
                return idIt->get<std::string>();
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Leave empty - malformed/unexpected response body.
    }
    return std::nullopt;
}

std::optional<std::string> AppleSearchAlbumArtLookup::CoverArtArchiveFrontUrl(const std::string& releaseId) {
    auto body = PerformGet("https://coverartarchive.org/release/" + releaseId);
    if (!body) {
        // Also covers the common case of no art on file at all - the
        // Archive returns 404 for a release with no cover art, which
        // PerformGet already treats as "no data".
        return std::nullopt;
    }
    try {
        auto json = nlohmann::json::parse(*body);
        auto imagesIt = json.find("images");
        if (imagesIt == json.end() || !imagesIt->is_array()) {
            return std::nullopt;
        }
        for (const auto& image : *imagesIt) {
            auto frontIt = image.find("front");
            if (frontIt != image.end() && frontIt->is_boolean() && frontIt->get<bool>()) {
                std::string art;
                auto thumbIt = image.find("thumbnails");
                if (thumbIt != image.end()) {
                    auto largeIt = thumbIt->find("large");
                    if (largeIt != thumbIt->end() && largeIt->is_string()) {
                        art = largeIt->get<std::string>();
                    }
                }
                if (art.empty()) {
                    auto imageIt = image.find("image");
                    if (imageIt != image.end() && imageIt->is_string()) {
                        art = imageIt->get<std::string>();
                    }
                }
                if (!art.empty()) {
                    // The Archive's API returns plain http:// - upgrade to
                    // https, matching every other URL this app hands to
                    // Discord.
                    if (art.rfind("http://", 0) == 0) {
                        art = "https://" + art.substr(7);
                    }
                    return art;
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Leave empty - malformed/unexpected response body.
    }
    return std::nullopt;
}

} // namespace platform_macos
