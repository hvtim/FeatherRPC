#pragma once

namespace core {

// Per-platform login-autostart control (Startup-folder shortcut on
// Windows, LaunchAgent on macOS, .desktop autostart entry on Linux).
class AutoLaunch {
public:
    virtual ~AutoLaunch() = default;

    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;
};

} // namespace core
