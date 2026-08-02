#include "core/ConfigPaths.h"

#include <cstdlib>

namespace core {

namespace {

std::filesystem::path HomeDirectory() {
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home);
    }
    return std::filesystem::path();
}

} // namespace

std::filesystem::path GetConfigDirectory() {
    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME"); xdgConfig && *xdgConfig) {
        return std::filesystem::path(xdgConfig) / "FeatherRPC";
    }
    return HomeDirectory() / ".config" / "FeatherRPC";
}

std::filesystem::path GetConfigFilePath() {
    return GetConfigDirectory() / "config.json";
}

std::filesystem::path GetLogFilePath() {
    return GetConfigDirectory() / "featherrpc.log";
}

std::filesystem::path GetPidFilePath() {
    return GetConfigDirectory() / "featherrpcd.pid";
}

std::filesystem::path GetStatusFilePath() {
    return GetConfigDirectory() / "featherrpcd.status.json";
}

std::filesystem::path GetCrashFilePath() {
    return GetConfigDirectory() / "featherrpc-crash.log";
}

} // namespace core
