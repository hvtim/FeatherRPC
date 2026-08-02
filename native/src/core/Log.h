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

    // Verbose Logging (tray Settings toggle) - gates extra per-poll detail
    // in PresenceEngine and the media-source implementations. Not used by
    // Write() itself; callers check IsVerbose() before deciding whether to
    // call Write() at all for a given line.
    static void SetVerbose(bool verbose);
    static bool IsVerbose();

private:
    static std::filesystem::path s_path;
    // Guards both the stdout write and the file-write section below -
    // Write() is called concurrently from PresenceEngine's worker thread,
    // the tray's media-source-refresh thread, and the UI/main thread, with
    // no synchronization previously; this fixes interleaved-line output
    // and a TOCTOU race in the old size-check-then-truncate logic.
    static std::mutex s_mutex;
    static std::atomic<bool> s_verbose;
    // Opened once in Init(), reused across every Write() call instead of
    // opening/closing a fresh handle per line - only closed/reopened when
    // RotateIfNeeded() actually rotates. Always accessed under s_mutex, so
    // no separate synchronization of its own is needed.
    static std::ofstream s_file;
};

} // namespace core
