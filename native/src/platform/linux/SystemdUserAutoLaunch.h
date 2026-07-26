#pragma once

#include "core/AutoLaunch.h"

namespace platform_linux {

// Linux's real service-manager integration for headless mode - unlike
// Windows/macOS (login-launched process, no genuine service concept),
// systemd --user gives this a real enable/disable/status lifecycle. The
// tray/GUI mode still uses DesktopAutoLaunch (XDG autostart), not this -
// this class is only for the `featherrpc autostart` command against the
// featherrpc.service user unit (see install.sh, which writes it).
class SystemdUserAutoLaunch : public core::AutoLaunch {
public:
    bool IsEnabled() const override;
    void SetEnabled(bool enabled) override;
};

} // namespace platform_linux
