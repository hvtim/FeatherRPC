#include "LaunchAgentAutoLaunch.h"

#include "core/AppConfig.h"
#include "core/ConfigPaths.h"

#include <mach-o/dyld.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace platform_macos {

namespace {

std::filesystem::path PlistPath(const std::string& label) {
    const char* home = std::getenv("HOME");
    if (!home) {
        return {};
    }
    return std::filesystem::path(home) / "Library" / "LaunchAgents" / (label + ".plist");
}

// _NSGetExecutablePath over NSBundle - keeps this file plain C++ (no
// Objective-C runtime needed just to find our own path), same rationale
// as Windows' GetModuleFileNameW call in ShellLinkAutoLaunch.
std::string SelfExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size); // first call only reports the required size
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return "";
    }
    return std::string(buffer.data());
}

} // namespace

LaunchAgentAutoLaunch::LaunchAgentAutoLaunch(std::string targetExePath, std::string label)
    : _targetExePath(std::move(targetExePath)), _label(std::move(label)) {}

bool LaunchAgentAutoLaunch::IsEnabled() const {
    auto path = PlistPath(_label);
    return !path.empty() && std::filesystem::exists(path);
}

void LaunchAgentAutoLaunch::SetEnabled(bool enabled) {
    auto path = PlistPath(_label);
    if (path.empty()) {
        return;
    }

    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }

    std::string exe = _targetExePath.empty() ? SelfExecutablePath() : _targetExePath;
    if (exe.empty()) {
        return;
    }

    // Reflects the current tray preference in ProgramArguments rather
    // than needing a separate plist per mode - `featherrpc tray on/off`
    // re-runs SetEnabled(true) to keep this in sync whenever autostart is
    // already registered.
    core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());

    std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n"
        << "  <key>Label</key>\n"
        << "  <string>" << _label << "</string>\n"
        << "  <key>ProgramArguments</key>\n"
        << "  <array>\n"
        << "    <string>" << exe << "</string>\n";
    if (!config.trayEnabled) {
        out << "    <string>--no-tray</string>\n";
    }
    out << "  </array>\n"
        << "  <key>RunAtLoad</key>\n"
        << "  <true/>\n"
        << "</dict>\n"
        << "</plist>\n";
}

} // namespace platform_macos
