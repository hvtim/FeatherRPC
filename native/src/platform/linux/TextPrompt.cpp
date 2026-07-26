#include "TextPrompt.h"
#include "core/Log.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace platform_linux {

namespace {

bool CommandExists(const char* cmd) {
    std::string check = std::string("command -v ") + cmd + " >/dev/null 2>&1";
    return std::system(check.c_str()) == 0;
}

// Single-quotes the whole argument, escaping embedded quotes as '\'' - the
// standard POSIX-shell-safe quoting trick. Sufficient here since these
// commands are built from fixed literals plus this helper, never raw
// concatenation of untrusted text.
std::string ShellQuote(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

bool RunCapture(const std::string& command, std::string& output) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return false;

    std::array<char, 256> buf{};
    output.clear();
    while (fgets(buf.data(), buf.size(), pipe)) {
        output += buf.data();
    }
    int status = pclose(pipe);

    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return status == 0;
}

} // namespace

void PromptForText(const std::string& title, const std::string& prompt, std::string& value) {
    std::string command;
    if (CommandExists("zenity")) {
        command = "zenity --entry --title=" + ShellQuote(title) + " --text=" + ShellQuote(prompt) +
                  " --entry-text=" + ShellQuote(value) + " 2>/dev/null";
    } else if (CommandExists("kdialog")) {
        command = "kdialog --inputbox " + ShellQuote(prompt) + " " + ShellQuote(value) + " --title " +
                  ShellQuote(title) + " 2>/dev/null";
    } else {
        core::Log::Write(
            "[warn] Neither zenity nor kdialog is installed - edit config.json directly to change '" + prompt + "'.");
        return;
    }

    std::string output;
    if (RunCapture(command, output)) {
        value = output;
    }
    // Non-zero exit (Cancel) leaves value unchanged, matching the Windows prompt dialog's Cancel behavior.
}

} // namespace platform_linux
