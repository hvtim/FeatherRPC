#include "AppConfig.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace core {

// nlohmann::json's ADL-based serialization requires to_json/from_json to
// live in the same namespace as AppConfig itself.
void to_json(nlohmann::json& j, const AppConfig& c) {
    j = nlohmann::json{
        {"ClientId", c.clientId},
        {"LargeImageKey", c.largeImageKey},
        {"PollIntervalMs", c.pollIntervalMs},
        {"BroadcastEnabled", c.broadcastEnabled},
        {"ShowTrackNumber", c.showTrackNumber},
        {"ArtMode", c.artMode},
        {"CustomArtUrl", c.customArtUrl},
        {"MediaSource", c.mediaSource},
        {"TrayEnabled", c.trayEnabled},
        {"VerboseLogging", c.verboseLogging},
    };
}

void from_json(const nlohmann::json& j, AppConfig& c) {
    AppConfig defaults;
    c.clientId = j.value("ClientId", defaults.clientId);
    c.largeImageKey = j.value("LargeImageKey", defaults.largeImageKey);
    c.pollIntervalMs = j.value("PollIntervalMs", defaults.pollIntervalMs);
    c.broadcastEnabled = j.value("BroadcastEnabled", defaults.broadcastEnabled);
    c.showTrackNumber = j.value("ShowTrackNumber", defaults.showTrackNumber);
    c.artMode = j.value("ArtMode", defaults.artMode);
    c.customArtUrl = j.value("CustomArtUrl", defaults.customArtUrl);
    c.mediaSource = j.value("MediaSource", defaults.mediaSource);
    c.trayEnabled = j.value("TrayEnabled", defaults.trayEnabled);
    c.verboseLogging = j.value("VerboseLogging", defaults.verboseLogging);
}

AppConfig LoadConfig(const std::filesystem::path& path) {
    try {
        std::ifstream file(path);
        if (!file) {
            return AppConfig{};
        }
        nlohmann::json j;
        file >> j;
        return j.get<AppConfig>();
    } catch (const std::exception&) {
        return AppConfig{};
    }
}

void SaveConfig(const AppConfig& config, const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    nlohmann::json j = config;
    std::ofstream file(path);
    file << j.dump(2);
}

} // namespace core
