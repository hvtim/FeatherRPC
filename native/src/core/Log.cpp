#include "Log.h"

#include <cstdio>
#include <ctime>
#include <fstream>

namespace core {

namespace {
constexpr uintmax_t kMaxSizeBytes = 2 * 1024 * 1024;
}

std::filesystem::path Log::s_path;

void Log::Init(std::filesystem::path logFilePath) {
    s_path = std::move(logFilePath);
    // Windows installs never hit this gap in practice (the config dir was
    // always created earlier by a prior install's SaveConfig), but a
    // first-ever run on a fresh machine has no such directory yet -
    // std::ofstream silently no-ops on a missing parent, so Write() would
    // otherwise drop every log line without any indication why.
    std::error_code ec;
    std::filesystem::create_directories(s_path.parent_path(), ec);
}

void Log::Write(const std::string& message) {
    std::puts(message.c_str());

    if (s_path.empty()) {
        return;
    }

    try {
        std::error_code ec;
        if (std::filesystem::exists(s_path, ec) && std::filesystem::file_size(s_path, ec) > kMaxSizeBytes) {
            std::filesystem::remove(s_path, ec);
        }

        std::ofstream file(s_path, std::ios::app);
        if (!file) {
            return;
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

        file << timestamp << " " << message << "\n";
    } catch (const std::exception&) {
        // Logging is best-effort - never let a log write failure take down the app.
    }
}

} // namespace core
