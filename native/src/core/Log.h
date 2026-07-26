#pragma once

#include <filesystem>
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

private:
    static std::filesystem::path s_path;
};

} // namespace core
