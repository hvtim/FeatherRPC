#include "TextPrompt.h"
#include "DarkMode.h"
#include "resource.h"

#include <commctrl.h>

#include <iterator>

namespace nativeui {

namespace {

// Loaded once and kept for the process's lifetime (like the WM_CTLCOLOR
// brushes below) rather than reloaded/destroyed per dialog - the DLLs
// needed to decode icon.ico are already resident after the tray icon's
// own load, so this just adds one small decoded HICON, not a new DLL.
HICON LoadAppIcon(int cx, int cy) {
    HICON icon = nullptr;
    if (FAILED(LoadIconWithScaleDown(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON), cx, cy, &icon))) {
        return nullptr;
    }
    return icon;
}

// Classic dialog templates (EDITTEXT/BUTTON/static controls) don't
// auto-theme for dark mode - Windows never updated them, the same reason
// Notepad and other Win32 apps had to add this kind of handling by hand.
// Applied once from WM_INITDIALOG; the WM_CTLCOLORXXX handlers below do
// the rest.
void ApplyDarkModeIfEnabled(HWND hwnd) {
    if (!IsDarkModeEnabled()) {
        return;
    }
    ApplyDarkTitleBar(hwnd, true);
    ApplyDarkControlTheme(GetDlgItem(hwnd, IDOK), true);
    ApplyDarkControlTheme(GetDlgItem(hwnd, IDCANCEL), true);
    ApplyDarkControlTheme(GetDlgItem(hwnd, IDC_PROMPT_EDIT), true);
}

// The dialog's owner (the tray icon's message-only window) is never
// visible, so Windows has nothing sensible to center the dialog against
// and falls back to the template's literal (0,0) - the screen's top-left
// corner. Center on whichever monitor currently has the cursor instead,
// since that's where the user just right-clicked the tray icon.
void CenterOnCursorMonitor(HWND hwnd) {
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return;
    }

    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

struct PromptParams {
    const std::wstring* title = nullptr;
    const std::wstring* label = nullptr;
    std::wstring* result = nullptr;
    bool confirmed = false;
};

INT_PTR CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            auto* params = reinterpret_cast<PromptParams*>(lParam);
            SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(params));
            SetWindowTextW(hwnd, params->title->c_str());
            SetDlgItemTextW(hwnd, IDC_PROMPT_LABEL, params->label->c_str());
            SetDlgItemTextW(hwnd, IDC_PROMPT_EDIT, params->result->c_str());
            CenterOnCursorMonitor(hwnd);
            ApplyDarkModeIfEnabled(hwnd);

            static HICON smallIcon = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
            static HICON bigIcon = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
            if (smallIcon) {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
            }
            if (bigIcon) {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
            }
            HWND edit = GetDlgItem(hwnd, IDC_PROMPT_EDIT);
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return FALSE; // FALSE because we already set focus ourselves
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC: {
            if (!IsDarkModeEnabled()) {
                return FALSE;
            }
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, kDarkText);
            SetBkColor(hdc, kDarkBackground);
            static HBRUSH brush = CreateSolidBrush(kDarkBackground);
            return reinterpret_cast<INT_PTR>(brush);
        }
        case WM_CTLCOLOREDIT: {
            if (!IsDarkModeEnabled()) {
                return FALSE;
            }
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, kDarkText);
            SetBkColor(hdc, kDarkControlBackground);
            static HBRUSH brush = CreateSolidBrush(kDarkControlBackground);
            return reinterpret_cast<INT_PTR>(brush);
        }
        case WM_COMMAND: {
            auto* params = reinterpret_cast<PromptParams*>(GetWindowLongPtrW(hwnd, DWLP_USER));
            if (LOWORD(wParam) == IDOK) {
                if (params) {
                    wchar_t buffer[2048];
                    GetDlgItemTextW(hwnd, IDC_PROMPT_EDIT, buffer, static_cast<int>(std::size(buffer)));
                    *params->result = buffer;
                    params->confirmed = true;
                }
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            return FALSE;
        }
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        default:
            return FALSE;
    }
}

} // namespace

bool PromptForText(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value) {
    PromptParams params;
    params.title = &title;
    params.label = &label;
    params.result = &value;

    HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    if (!hInstance) {
        hInstance = GetModuleHandleW(nullptr);
    }

    INT_PTR result = DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_TEXT_PROMPT), owner,
        PromptDlgProc, reinterpret_cast<LPARAM>(&params));

    return result == IDOK && params.confirmed;
}

} // namespace nativeui
