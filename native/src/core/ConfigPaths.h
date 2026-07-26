#pragma once

#include <filesystem>

namespace core {

// Per-OS config/log directory resolution lives in platform/ (each
// platform's rule for "where does user config belong" is genuinely
// different - not a portability gap to abstract away further).
std::filesystem::path GetConfigDirectory();
std::filesystem::path GetConfigFilePath();
std::filesystem::path GetLogFilePath();

// Only meaningful on POSIX (Linux/macOS) - the CLI control tool reads this
// to find the running daemon's pid for kill(2). Windows uses named Event
// objects instead (existence-checked via OpenEventW), so this is
// implemented there too for a uniform interface but never read.
std::filesystem::path GetPidFilePath();

// JSON status blob the app writes on every PresenceEngine::OnStatusChanged
// firing, so `featherrpc status` is a fast file read with no IPC round-trip.
std::filesystem::path GetStatusFilePath();

} // namespace core
