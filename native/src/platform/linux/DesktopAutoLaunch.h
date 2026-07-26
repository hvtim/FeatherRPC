#pragma once

#include "core/AutoLaunch.h"

namespace platform_linux {

// Manages an XDG autostart .desktop entry - the Linux equivalent of the
// Windows Startup-folder shortcut.
class DesktopAutoLaunch : public core::AutoLaunch {
public:
    bool IsEnabled() const override;
    void SetEnabled(bool enabled) override;
};

} // namespace platform_linux
