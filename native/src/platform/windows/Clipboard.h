#pragma once

#include <windows.h>
#include <string>

namespace platform_windows {

// Puts text on the system clipboard as CF_UNICODETEXT. Best-effort, no
// return value.
void SetClipboardText(HWND owner, const std::wstring& text);

} // namespace platform_windows
