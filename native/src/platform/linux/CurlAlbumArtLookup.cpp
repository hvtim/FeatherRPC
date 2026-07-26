#include "CurlAlbumArtLookup.h"

#include <nlohmann/json.hpp>

#include <curl/curl.h>

#include <cctype>

namespace platform_linux {

namespace {

std::string UrlEncode(CURL* curl, const std::string& value) {
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = encoded ? encoded : value;
    if (encoded) curl_free(encoded);
    return result;
}

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

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

// Shared by the iTunes, MusicBrainz, and Cover Art Archive lookups below -
// same timeouts/user-agent, same "2xx or nothing" success rule. Returns the
// response body only on a 2xx status; anything else (404, timeout,
// malformed response) is treated as "no data", not an error to propagate.
std::optional<std::string> PerformGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    // MusicBrainz's API policy requires a descriptive user-agent identifying
    // the application, or requests get rate-limited more aggressively.
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "FeatherRPC/0.1 ( https://github.com/hvtim/FeatherRPC )");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && statusCode >= 200 && statusCode < 300) {
        return body;
    }
    return std::nullopt;
}

} // namespace

std::optional<std::string> CurlAlbumArtLookup::GetArtworkUrl(
    const std::string& artist, const std::string& track, const std::string& album) {
    std::string key = artist + "|" + track + "|" + album;
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }

    // Prefer an album-title search when available - matches iTunes/MPRIS
    // tagging, avoids a different single/compilation's cover art, and
    // every track on an album shares the same art anyway.
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

std::optional<std::string> CurlAlbumArtLookup::Lookup(const std::string& term, const std::string& entity) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;
    std::string url = "https://itunes.apple.com/search?term=" + UrlEncode(curl, Trim(term)) + "&entity=" + entity + "&limit=1";
    curl_easy_cleanup(curl);

    auto body = PerformGet(url);
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
                    // the filename - ask for something bigger than the
                    // default 100x100 thumbnail.
                    ReplaceAll(art, "100x100bb", "512x512bb");
                    result = art;
                }
            }
        }
    } catch (const nlohmann::json::exception&) {
        // Leave result empty - malformed/unexpected response body.
    }

    return result;
}

std::optional<std::string> CurlAlbumArtLookup::LookupMusicBrainz(
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

std::optional<std::string> CurlAlbumArtLookup::FindReleaseIdByAlbum(
    const std::string& artist, const std::string& album) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;
    std::string query = "artist:\"" + Trim(artist) + "\" AND release:\"" + Trim(album) + "\"";
    std::string url = "https://musicbrainz.org/ws/2/release/?query=" + UrlEncode(curl, query) + "&fmt=json&limit=1";
    curl_easy_cleanup(curl);

    auto body = PerformGet(url);
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

std::optional<std::string> CurlAlbumArtLookup::FindReleaseIdByRecording(
    const std::string& artist, const std::string& track) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;
    std::string query = "artist:\"" + Trim(artist) + "\" AND recording:\"" + Trim(track) + "\"";
    std::string url = "https://musicbrainz.org/ws/2/recording/?query=" + UrlEncode(curl, query) + "&fmt=json&limit=1";
    curl_easy_cleanup(curl);

    auto body = PerformGet(url);
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

std::optional<std::string> CurlAlbumArtLookup::CoverArtArchiveFrontUrl(const std::string& releaseId) {
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

} // namespace platform_linux
