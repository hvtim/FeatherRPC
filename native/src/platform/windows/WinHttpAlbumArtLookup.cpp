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

    _cache[key] = url;
    return url;
}

std::optional<std::string> WinHttpAlbumArtLookup::Lookup(const std::string& term, const std::string& entity) {
    HINTERNET session = WinHttpOpen(L"FeatherRPC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        return std::nullopt;
    }

    // 5-second timeouts (resolve/connect/send/receive) - matches the C#
    // HttpClient's 5-second Timeout for this same lookup.
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

    HINTERNET connection = WinHttpConnect(session, L"itunes.apple.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    std::wstring path =
        L"/search?term=" + WideFromNarrow(UrlEncode(Trim(term))) + L"&entity=" + WideFromNarrow(entity) + L"&limit=1";

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

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

} // namespace platform_windows
