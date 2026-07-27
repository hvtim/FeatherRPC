#include "SystemdUserAutoLaunch.h"

#include <sys/wait.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace platform_linux {

namespace {

// User-scope unit, deliberately: `systemctl --user ...`, never
// `sudo systemctl ...` - install.sh only ever registers it under
// $XDG_CONFIG_HOME/systemd/user, there is no system-wide unit by this name.
constexpr const char* kUnitName = "featherrpc.service";

// std::system's return value is the raw wait status, not the process exit
// code - WEXITSTATUS is required to get the real code systemctl returned.
int RunSystemctl(const std::string& args) {
    std::string command = std::string("systemctl --user ") + args + " >/dev/null 2>&1";
    int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

bool SystemdUserAutoLaunch::IsEnabled() const {
    return RunSystemctl(std::string("is-enabled ") + kUnitName) == 0;
}

void SystemdUserAutoLaunch::SetEnabled(bool enabled) {
    RunSystemctl(std::string(enabled ? "enable --now " : "disable --now ") + kUnitName);
    // Printed unconditionally (not just on failure) - a `sudo systemctl
    // restart featherrpc` typo against this exact unit is the concrete
    // mistake this line exists to head off; spelling out the correct
    // invocation here beats a "not found" error and a confused user later.
    std::cout << "(manage it directly with: systemctl --user status " << kUnitName << ")\n";
}

} // namespace platform_linux
