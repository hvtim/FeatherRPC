#include "WinHttpAlbumArtLookup.h"
#include "StringConvert.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <winhttp.h>

#include <cctype>

namespace platform_windows {

namespace {

// Percent-encodes a query term for use in a URL - equivalent to .NET's
// Uri.EscapeDataString for the ASCII-unreserved-character set.
std::string UrlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0xF];
        }
    }
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

// Shared by the iTunes, MusicBrainz, and Cover Art Archive lookups below -
// same timeouts/user-agent, same "2xx or nothing" success rule. Returns the
// response body only on a 2xx status; anything else (404, timeout,
// malformed response) is treated as "no data", not an error to propagate.
std::optional<std::string> PerformGet(const std::wstring& host, const std::wstring& path) {
    // MusicBrainz's API policy requires a descriptive user-agent
    // identifying the application, or requests get rate-limited more
    // aggressively.
    HINTERNET session = WinHttpOpen(L"FeatherRPC/0.1 ( https://github.com/hvtim/FeatherRPC )",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        return std::nullopt;
    }
    // 5-second timeouts (resolve/connect/send/receive) - matches the C#
    // HttpClient's 5-second Timeout for this same lookup.
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(request, nullptr);

    if (ok) {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        ok = statusCode >= 200 && statusCode < 300;
    }

    std::optional<std::string> result;
    if (ok) {
        std::string body;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD bytesRead = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &bytesRead)) {
                break;
            }
            chunk.resize(bytesRead);
            body += chunk;
        }
        result = body;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

} // namespace

std::optional<std::string> WinHttpAlbumArtLookup::GetArtworkUrl(
    const std::string& artist, const std::string& track, const std::string& album) {
    std::string key = artist + "|" + track + "|" + album;
    auto it = _cache.find(key);
    if (it != _cache.end()) {
        return it->second;
    }

    // Prefer an album-title search when we have one: matches iTunes' own
    // tagging, avoids picking up a different single/compilation's cover
    // art, and sidesteps unusually-formatted track titles. Every track on
    // an album shares the same cover art anyway.
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

std::optional<std::string> WinHttpAlbumArtLookup::Lookup(const std::string& term, const std::string& entity) {
    std::wstring path =
        L"/search?term=" + WideFromNarrow(UrlEncode(Trim(term))) + L"&entity=" + WideFromNarrow(entity) + L"&limit=1";
    auto body = PerformGet(L"itunes.apple.com", path);
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

std::optional<std::string> WinHttpAlbumArtLookup::LookupMusicBrainz(
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

std::optional<std::string> WinHttpAlbumArtLookup::FindReleaseIdByAlbum(
    const std::string& artist, const std::string& album) {
    std::string query = "artist:\"" + Trim(artist) + "\" AND release:\"" + Trim(album) + "\"";
    std::wstring path = L"/ws/2/release/?query=" + WideFromNarrow(UrlEncode(query)) + L"&fmt=json&limit=1";
    auto body = PerformGet(L"musicbrainz.org", path);
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

std::optional<std::string> WinHttpAlbumArtLookup::FindReleaseIdByRecording(
    const std::string& artist, const std::string& track) {
    std::string query = "artist:\"" + Trim(artist) + "\" AND recording:\"" + Trim(track) + "\"";
    std::wstring path = L"/ws/2/recording/?query=" + WideFromNarrow(UrlEncode(query)) + L"&fmt=json&limit=1";
    auto body = PerformGet(L"musicbrainz.org", path);
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

std::optional<std::string> WinHttpAlbumArtLookup::CoverArtArchiveFrontUrl(const std::string& releaseId) {
    auto body = PerformGet(L"coverartarchive.org", L"/release/" + WideFromNarrow(releaseId));
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

} // namespace platform_windows
