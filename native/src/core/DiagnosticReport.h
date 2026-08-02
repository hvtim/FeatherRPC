#pragma once

#include "AppConfig.h"

#include <optional>
#include <string>
#include <vector>

namespace core {

struct DiagnosticReportInputs {
    AppConfig config;
    std::string osDescription;          // e.g. "Windows 11 24H2 (build 26100), x64"
    std::string mediaSourceLiveCheckText; // pre-formatted by the calling platform - see e.g. platform_windows::LiveMediaSourceCheck
    std::vector<std::string> recentLogLines; // tail of the log file, most-recent last
    std::optional<std::string> lastCrashReportText; // contents of GetCrashFilePath(), if it exists
};

// Assembles the text the tray's "Copy Diagnostic Info" action puts on the
// clipboard. Platform-agnostic - the caller gathers and formats every
// platform-specific piece before passing it in.
std::string BuildDiagnosticReport(const DiagnosticReportInputs& inputs);

} // namespace core
