#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace core {

// Writes to both stdout (useful run from a terminal) and a log file next
// to the exe - the only way to see anything once the app is running
// windowless via autorun.
class Log {
public:
    // Must be called once before any Write() call; sets the log file path.
    static void Init(std::filesystem::path logFilePath);
    static void Write(const std::string& message);

    // Gates extra per-poll detail in PresenceEngine and media sources.
    // Callers check IsVerbose() themselves before calling Write().
    static void SetVerbose(bool verbose);
    static bool IsVerbose();

private:
    static std::filesystem::path s_path;
    // Write() is called concurrently from PresenceEngine's worker thread,
    // the tray's media-refresh thread, and the UI thread.
    static std::mutex s_mutex;
    static std::atomic<bool> s_verbose;
    // Opened once in Init(), reused across writes; only closed/reopened
    // when RotateIfNeeded() rotates. Always accessed under s_mutex.
    static std::ofstream s_file;
};

} // namespace core
