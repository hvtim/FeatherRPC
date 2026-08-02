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

// Where a crash handler writes its report - deliberately a separate file
// from GetLogFilePath(), not appended to the main log: the crash handlers
// (CrashHandler.cpp, Windows and POSIX) must never touch core::Log or the
// main log file at all, since the crashing thread could have it open via
// std::ofstream at the exact moment of the crash. Checked for existence by
// DiagnosticReport::BuildDiagnosticReport() and folded into "Copy
// Diagnostic Info" when present.
std::filesystem::path GetCrashFilePath();

} // namespace core
