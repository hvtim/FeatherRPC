#include "StatusFile.h"

#include "core/ConfigPaths.h"

#include <nlohmann/json.hpp>

#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cli {

namespace {

// Matches the #ifdef _WIN32/getpid split already accepted in
// core/DiscordIpcClient.cpp - a minor CRT difference, not worth a
// platform/ abstraction for one integer.
long CurrentPid() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

} // namespace

void WriteStatusFile(const std::string& status) {
    nlohmann::json j{{"status", status}, {"pid", CurrentPid()}};
    std::ofstream file(core::GetStatusFilePath());
    file << j.dump();
}

std::optional<StatusFileContents> ReadStatusFile() {
    std::ifstream file(core::GetStatusFilePath());
    if (!file) {
        return std::nullopt;
    }
    try {
        nlohmann::json j;
        file >> j;
        StatusFileContents contents;
        contents.status = j.value("status", std::string());
        contents.pid = j.value("pid", 0L);
        return contents;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace cli
