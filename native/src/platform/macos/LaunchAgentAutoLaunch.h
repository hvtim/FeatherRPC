#pragma once

#include "core/AutoLaunch.h"

#include <string>

namespace platform_macos {

// Manages a per-user LaunchAgent plist for login autostart via plain file
// I/O - a simple RunAtLoad agent doesn't need the ServiceManagement
// framework's registration APIs.
class LaunchAgentAutoLaunch : public core::AutoLaunch {
public:
    // Empty targetExePath means "this process's own executable"
    // (_NSGetExecutablePath) - the tray app's existing default behavior.
    // The CLI tool passes an explicit path so it registers autostart for
    // the real app binary, not for its own short-lived process.
    explicit LaunchAgentAutoLaunch(std::string targetExePath = "",
        std::string label = "com.hvtim.featherrpc");

    bool IsEnabled() const override;
    void SetEnabled(bool enabled) override;

private:
    std::string _targetExePath;
    std::string _label;
};

} // namespace platform_macos
