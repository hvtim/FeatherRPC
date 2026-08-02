#include "DiagnosticReport.h"
#include "core/Version.h"

#include <ctime>
#include <sstream>

namespace core {

namespace {

std::string CurrentTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return buf;
}

} // namespace

std::string BuildDiagnosticReport(const DiagnosticReportInputs& inputs) {
    std::ostringstream out;

    out << "FeatherRPC diagnostic report\n";
    out << "Version: " << kBuildString << "\n";
    out << "Generated: " << CurrentTimestamp() << "\n";
    out << "Platform: " << inputs.osDescription << "\n";
    out << "\n";

    out << "-- Config --\n";
    // Discord Application IDs are public identifiers, not secrets (unlike
    // a bot token/client secret, neither of which this app ever stores) -
    // see docs/KnownIssues.md's "Client ID masking" entry for the project's
    // own final conclusion on this after some back-and-forth. Included
    // unredacted since a maintainer diagnosing an ID-specific bug
    // genuinely needs it.
    out << "Client ID: " << inputs.config.clientId << "\n";
    out << "Media source: " << inputs.config.mediaSource << "\n";
    out << "Poll interval: " << inputs.config.pollIntervalMs << "ms\n";
    out << "Broadcast enabled: " << (inputs.config.broadcastEnabled ? "yes" : "no") << "\n";
    out << "Show track number: " << (inputs.config.showTrackNumber ? "yes" : "no") << "\n";
    out << "Art mode: " << inputs.config.artMode << "\n";
    if (inputs.config.artMode == "Custom") {
        out << "Custom art URL: " << inputs.config.customArtUrl << "\n";
    }
    out << "Large image key: " << inputs.config.largeImageKey << "\n";
    out << "Verbose logging: " << (inputs.config.verboseLogging ? "on" : "off") << "\n";
    out << "\n";

    out << "-- Live media source check --\n";
    out << inputs.mediaSourceLiveCheckText << "\n";
    out << "\n";

    out << "-- Recent log lines --\n";
    if (inputs.recentLogLines.empty()) {
        out << "(none)\n";
    } else {
        for (const auto& line : inputs.recentLogLines) {
            out << line << "\n";
        }
    }

    if (inputs.lastCrashReportText.has_value()) {
        out << "\n";
        out << "-- Last crash report --\n";
        out << "(may be from a previous, unrelated session - check the timestamp inside it)\n";
        out << *inputs.lastCrashReportText << "\n";
    }

    return out.str();
}

} // namespace core
