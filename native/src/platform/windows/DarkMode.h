#pragma once

#include <windows.h>

namespace nativeui {

// Colors used across dark-mode-aware native dialogs - not a full theme
// system, just enough to keep the two-field text prompt from looking like
// a jarring light-mode popup on an otherwise-dark desktop.
constexpr COLORREF kDarkBackground = RGB(32, 32, 32);
constexpr COLORREF kDarkControlBackground = RGB(45, 45, 45);
constexpr COLORREF kDarkText = RGB(255, 255, 255);

// Reads HKCU's AppsUseLightTheme setting directly - there is no public,
// documented API for "is the system in dark mode" as of this writing, so
// this is the same approach every non-UWP Win32 app takes (Explorer's own
// setting lives in the same key). Missing key (pre-Windows 10 1809) means
// no dark mode concept exists - treated as light.
bool IsDarkModeEnabled();

// Colors the window's title bar/frame via DWM - the classic client-area
// controls (EDITTEXT, BUTTON, static labels) need separate handling via
// WM_CTLCOLORXXX in the dialog procedure, this only affects the frame.
void ApplyDarkTitleBar(HWND hwnd, bool dark);

// Applies the "DarkMode_Explorer" visual style to a single control (most
// useful for BUTTON controls, where it noticeably improves the pushbutton
// appearance in dark mode with no other code needed). Pass dark=false to
// remove it and restore the default theme.
void ApplyDarkControlTheme(HWND control, bool dark);

// Dark-themes native popup menus for the process's lifetime. No public API
// exists for this - uses uxtheme.dll ordinals 135/136 (undocumented,
// same technique VS Code/Windows Terminal use). Risk accepted: if a future
// Windows update removes these, GetProcAddress returns null and this
// no-ops, falling back to light menus rather than crashing. Call once at
// startup, before any menu is shown.
void EnableDarkModeForMenus();

} // namespace nativeui
