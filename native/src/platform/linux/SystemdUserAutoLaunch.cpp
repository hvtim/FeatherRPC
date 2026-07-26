#include "SystemdUserAutoLaunch.h"

#include <sys/wait.h>

#include <cstdlib>
#include <string>

namespace platform_linux {

namespace {

// std::system's return value is the raw wait status, not the process exit
// code - WEXITSTATUS is required to get the real code systemctl returned.
int RunSystemctl(const char* args) {
    std::string command = std::string("systemctl --user ") + args + " >/dev/null 2>&1";
    int status = std::system(command.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

bool SystemdUserAutoLaunch::IsEnabled() const {
    return RunSystemctl("is-enabled featherrpcd.service") == 0;
}

void SystemdUserAutoLaunch::SetEnabled(bool enabled) {
    RunSystemctl(enabled ? "enable --now featherrpcd.service" : "disable --now featherrpcd.service");
}

} // namespace platform_linux
