#include "DesktopAutoLaunch.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace platform_linux {

namespace {

std::filesystem::path AutostartDir() {
    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME"); xdgConfig && *xdgConfig) {
        return std::filesystem::path(xdgConfig) / "autostart";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "") / ".config" / "autostart";
}

std::filesystem::path DesktopFilePath() {
    return AutostartDir() / "FeatherRPC.desktop";
}

std::filesystem::path CurrentExecutablePath() {
    // PATH_MAX isn't guaranteed to be defined (POSIX allows filesystems
    // with no maximum path length) - 4096 covers every real Linux
    // filesystem in practice, which is what PATH_MAX itself would resolve
    // to here anyway.
    char buf[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return len > 0 ? std::filesystem::path(std::string(buf, static_cast<size_t>(len))) : std::filesystem::path();
}

} // namespace

bool DesktopAutoLaunch::IsEnabled() const {
    return std::filesystem::exists(DesktopFilePath());
}

void DesktopAutoLaunch::SetEnabled(bool enabled) {
    auto path = DesktopFilePath();

    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }

    auto exePath = CurrentExecutablePath();
    if (exePath.empty()) return;

    std::error_code ec;
    std::filesystem::create_directories(AutostartDir(), ec);

    std::ofstream file(path, std::ios::trunc);
    if (!file) return;
    file << "[Desktop Entry]\n"
         << "Type=Application\n"
         << "Name=FeatherRPC\n"
         << "Comment=Now-playing sync for Discord Rich Presence\n"
         << "Exec=\"" << exePath.string() << "\"\n"
         << "Icon=featherrpc\n"
         << "X-GNOME-Autostart-enabled=true\n";
}

} // namespace platform_linux
