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

    _cache[key] = url;
    return url;
}

std::optional<std::string> CurlAlbumArtLookup::Lookup(const std::string& term, const std::string& entity) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string url = "https://itunes.apple.com/search?term=" + UrlEncode(curl, Trim(term)) + "&entity=" + entity + "&limit=1";

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "FeatherRPC/1.0");
    // Matches the 5-second timeout used by the C# HttpClient and the
    // Windows WinHTTP port of this same lookup.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    std::optional<std::string> result;
    if (res == CURLE_OK && statusCode >= 200 && statusCode < 300) {
        try {
            auto json = nlohmann::json::parse(body);
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
    }

    return result;
}

} // namespace platform_linux
