#include "DarkMode.h"

#include <dwmapi.h>
#include <uxtheme.h>

namespace nativeui {

namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

// Only defined in newer Windows SDKs - declare locally so this builds
// against older SDK headers too. 20 is the modern value (Windows 10
// 20H1+); 19 is the pre-20H1 value used as a fallback below.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_OLD
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD 19
#endif

} // namespace

bool IsDarkModeEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = REG_DWORD;
    bool ok = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
        reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS;
    RegCloseKey(key);

    return ok && value == 0;
}

void ApplyDarkTitleBar(HWND hwnd, bool dark) {
    BOOL enabled = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled)))) {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &enabled, sizeof(enabled));
    }
}

void ApplyDarkControlTheme(HWND control, bool dark) {
    SetWindowTheme(control, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
}

void EnableDarkModeForMenus() {
    if (!IsDarkModeEnabled()) {
        return;
    }

    HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme) {
        return;
    }

    enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
    using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
    using FlushMenuThemesFn = void(WINAPI*)();

    auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(
        GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));

    if (setPreferredAppMode) {
        setPreferredAppMode(PreferredAppMode::AllowDark);
    }
    if (flushMenuThemes) {
        flushMenuThemes();
    }
}

} // namespace nativeui
