#pragma once

#include <string>

namespace platform_linux {

// Copies text to the clipboard for the tray's "Copy Diagnostic Info"
// action. No GTK/Qt is linked (deliberate dependency philosophy - see
// SniTray.h's own comment on avoiding libayatana-appindicator), and
// there's no broadly-deployed clipboard portal for arbitrary D-Bus apps,
// so this shells out the same way TextPrompt.cpp already does: prefers
// wl-copy under Wayland, then xclip, then xsel. If none succeed, writes
// the text to a file under the config directory instead.
//
// Fully asynchronous, via GSubprocess (GLib's own async subprocess API -
// gio-2.0 is already a hard, REQUIRED dependency of this binary via
// SniTray.cpp's GDBus-based tray, so this adds no new dependency). Tries
// each candidate in order, advancing to the next only on a real,
// GLib-reported exit failure - never on a guess, and never by blocking
// the caller. Returns immediately; success/failure/fallback-to-file are
// all handled internally, with any failure logged via core::Log. An
// earlier version of this file used popen()/pclose(), which blocks until
// the child exits - fine for xclip/xsel (which exit after reading
// stdin), but wl-copy is designed to keep running indefinitely to keep
// serving the Wayland clipboard, so pclose() would block forever and
// freeze the entire tray (confirmed live - see docs/KnownIssues.md).
void CopyToClipboard(const std::string& text);

}  // namespace platform_linux
