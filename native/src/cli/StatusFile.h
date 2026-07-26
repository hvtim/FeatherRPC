#pragma once

#include <optional>
#include <string>

namespace cli {

// Written by the app itself on every PresenceEngine::OnStatusChanged
// firing, so `featherrpc status` is a fast file read with no IPC
// round-trip to a possibly-not-running instance.
void WriteStatusFile(const std::string& status);

struct StatusFileContents {
    std::string status;
    long pid = 0;
};

// std::nullopt if the file doesn't exist or fails to parse.
std::optional<StatusFileContents> ReadStatusFile();

} // namespace cli
