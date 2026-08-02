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

// Assembles the block of text the tray's "Copy Diagnostic Info" action puts
// on the clipboard - the whole point is that a reporter pastes this one
// block into the bug-report issue form's "Diagnostic Info" field instead of
// manually transcribing version/platform/config into separate fields.
// Platform-agnostic (no #ifdefs) - every platform-specific piece
// (osDescription, mediaSourceLiveCheckText, log tail, crash file) is
// gathered by the caller and passed in already-formatted.
std::string BuildDiagnosticReport(const DiagnosticReportInputs& inputs);

} // namespace core
