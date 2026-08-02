#include "Log.h"
#include "CrashRing.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>

namespace core {

namespace {
constexpr uintmax_t kMaxSizeBytes = 2 * 1024 * 1024;
constexpr int kMaxBackups = 3; // featherrpc.log.1 .. featherrpc.log.3

// Renames path -> path.1, path.1 -> path.2, ... dropping the oldest.
// Closes the file handle before renaming (Windows can't rename an open
// file without FILE_SHARE_DELETE) and reopens after.
void RotateIfNeeded(const std::filesystem::path& path, std::ofstream& file) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || std::filesystem::file_size(path, ec) <= kMaxSizeBytes) {
        return;
    }

    file.close();

    std::filesystem::path oldest = path;
    oldest += "." + std::to_string(kMaxBackups);
    std::filesystem::remove(oldest, ec);

    for (int i = kMaxBackups - 1; i >= 1; --i) {
        std::filesystem::path from = path;
        from += "." + std::to_string(i);
        std::filesystem::path to = path;
        to += "." + std::to_string(i + 1);
        std::filesystem::rename(from, to, ec);
    }

    std::filesystem::path first = path;
    first += ".1";
    std::filesystem::rename(path, first, ec);

    file.open(path, std::ios::app);
}
} // namespace

std::filesystem::path Log::s_path;
std::mutex Log::s_mutex;
std::atomic<bool> Log::s_verbose{false};
std::ofstream Log::s_file;

void Log::Init(std::filesystem::path logFilePath) {
    s_path = std::move(logFilePath);
    // std::ofstream silently no-ops on a missing parent dir.
    std::error_code ec;
    std::filesystem::create_directories(s_path.parent_path(), ec);
    s_file.open(s_path, std::ios::app);
}

void Log::SetVerbose(bool verbose) {
    s_verbose.store(verbose, std::memory_order_relaxed);
}

bool Log::IsVerbose() {
    return s_verbose.load(std::memory_order_relaxed);
}

void Log::Write(const std::string& message) {
    // Populate the crash ring buffer before taking any lock - it must
    // stay readable from a crash handler that could fire mid-Write().
    uint32_t idx = g_crashRing.writeIndex.fetch_add(1, std::memory_order_relaxed) % kCrashRingSlots;
    size_t copyLen = std::min(message.size(), kCrashRingSlotSize - 1);
    std::memcpy(g_crashRing.slots[idx], message.data(), copyLen);
    g_crashRing.slots[idx][copyLen] = '\0';

    std::lock_guard<std::mutex> lock(s_mutex);

    std::puts(message.c_str());

    if (s_path.empty()) {
        return;
    }

    try {
        RotateIfNeeded(s_path, s_file);

        if (!s_file.good()) {
            // Recover from a transient I/O error rather than staying
            // broken for the rest of the process's life.
            s_file.close();
            s_file.clear();
            s_file.open(s_path, std::ios::app);
            if (!s_file) {
                return;
            }
        }

        std::time_t t = std::time(nullptr);
        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif
        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tmBuf);

        s_file << timestamp << " " << message << "\n";
        // The handle is long-lived now, not destroyed (and flushed) per
        // line - flush explicitly so a crash can't lose buffered lines.
        s_file.flush();
    } catch (const std::exception&) {
        // Logging is best-effort.
    }
}

} // namespace core
