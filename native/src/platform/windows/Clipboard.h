#pragma once

#include <windows.h>
#include <string>

namespace platform_windows {

// Puts text on the system clipboard as CF_UNICODETEXT. Best-effort - no
// return value, matching this codebase's general "diagnostics/UX
// convenience calls shouldn't need elaborate error handling" style (see
// e.g. TrayIcon's CMD_OPEN_APP_DIR just fire-and-forgetting ShellExecuteW).
void SetClipboardText(HWND owner, const std::wstring& text);

} // namespace platform_windows
