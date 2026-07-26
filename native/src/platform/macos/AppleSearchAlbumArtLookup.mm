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

    _cache[key] = url;
    return url;
}

std::optional<std::string> AppleSearchAlbumArtLookup::Lookup(const std::string& term, const std::string& entity) {
    @autoreleasepool {
        NSString* nsTerm = [NSString stringWithUTF8String:Trim(term).c_str()];
        NSString* nsEntity = [NSString stringWithUTF8String:entity.c_str()];
        if (!nsTerm || !nsEntity) {
            return std::nullopt;
        }

        NSCharacterSet* allowed = [NSCharacterSet URLQueryAllowedCharacterSet];
        NSString* encodedTerm = [nsTerm stringByAddingPercentEncodingWithAllowedCharacters:allowed];

        NSString* urlString = [NSString stringWithFormat:@"https://itunes.apple.com/search?term=%@&entity=%@&limit=1",
            encodedTerm, nsEntity];
        NSURL* url = [NSURL URLWithString:urlString];
        if (!url) {
            return std::nullopt;
        }

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
        // Matches the 5-second timeout used by WinHTTP on Windows / the old
        // C# build's HttpClient.Timeout for this same lookup.
        request.timeoutInterval = 5.0;

        __block std::optional<std::string> result;
        dispatch_semaphore_t sema = dispatch_semaphore_create(0);

        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:request
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                  if (!error && data) {
                      NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
                      if (http.statusCode >= 200 && http.statusCode < 300) {
                          try {
                              const char* bytes = static_cast<const char*>(data.bytes);
                              auto json = nlohmann::json::parse(bytes, bytes + data.length);
                              auto resultsIt = json.find("results");
                              if (resultsIt != json.end() && resultsIt->is_array() && !resultsIt->empty()) {
                                  auto artIt = (*resultsIt)[0].find("artworkUrl100");
                                  if (artIt != (*resultsIt)[0].end() && artIt->is_string()) {
                                      std::string art = artIt->get<std::string>();
                                      if (!art.empty()) {
                                          // Apple's CDN accepts an arbitrary
                                          // size baked into the filename -
                                          // ask for bigger than the default
                                          // 100x100 thumbnail.
                                          ReplaceAll(art, "100x100bb", "512x512bb");
                                          result = art;
                                      }
                                  }
                              }
                          } catch (const nlohmann::json::exception&) {
                              // Leave result empty - malformed/unexpected body.
                          }
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

} // namespace platform_macos
