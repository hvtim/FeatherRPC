#pragma once

#include "core/AutoLaunch.h"

#include <filesystem>
#include <string>

namespace platform_windows {

// Manages the Startup-folder shortcut via native IShellLinkW COM - no
// Windows Script Host dependency, unlike the old WScript.Shell approach.
class ShellLinkAutoLaunch : public core::AutoLaunch {
public:
    // Empty targetExePath means "the currently running process's own
    // exe" (GetModuleFileNameW) - the tray app's existing default
    // behavior. The CLI tool passes an explicit path so it registers
    // autostart for the real app binary, not for its own short-lived
    // process.
    explicit ShellLinkAutoLaunch(std::filesystem::path targetExePath = {},
        std::wstring shortcutName = L"FeatherRPC.lnk");

    bool IsEnabled() const override;
    void SetEnabled(bool enabled) override;

private:
    std::filesystem::path _targetExePath;
    std::wstring _shortcutName;
};

} // namespace platform_windows
