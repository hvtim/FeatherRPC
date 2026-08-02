#include "Clipboard.h"

#include <cstring>

namespace platform_windows {

void SetClipboardText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return;
    }

    EmptyClipboard();

    // +1 for the null terminator - GlobalAlloc's size is in bytes, not
    // wchar_t count.
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return;
    }

    void* dst = GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        CloseClipboard();
        return;
    }
    memcpy(dst, text.c_str(), bytes);
    GlobalUnlock(mem);

    // Ownership of `mem` transfers to the clipboard on success - do not
    // GlobalFree() it either way (SetClipboardData already frees it on
    // failure too, per its documented contract).
    SetClipboardData(CF_UNICODETEXT, mem);

    CloseClipboard();
}

} // namespace platform_windows
