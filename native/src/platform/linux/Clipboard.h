#pragma once

#include <string>

namespace platform_linux {

// Copies text to the clipboard for the tray's "Copy Diagnostic Info"
// action. No GTK/Qt linked, so this shells out to wl-copy/xclip/xsel
// (preferring wl-copy under Wayland), falling back to a file under the
// config directory if none succeed.
//
// Fully asynchronous via GSubprocess. Returns immediately; success,
// failure, and file-fallback are all handled internally and logged via
// core::Log. See docs/KnownIssues.md for why this isn't popen()/pclose().
void CopyToClipboard(const std::string& text);

}  // namespace platform_linux
