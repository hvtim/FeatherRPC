#pragma once

namespace platform_windows {

// True if Windows will actually surface a notification for this app -
// either genuinely enabled, or the check itself was inconclusive (fails
// open rather than silently suppressing a real first-run notification).
// Uses the WinRT toast-notification permission API rather than reading
// any registry value directly - see TrayIcon.cpp's ShowFirstRunBalloon()
// for why. Kept in its own translation unit (not TrayIcon.cpp) because
// TrayIcon.h pulls in <shellapi.h>, which conflicts with the WinRT
// headers this needs when both land in one file - same reason
// SmtcMediaSource.cpp forward-declares its own Shell32 functions instead
// of including <shellapi.h>.
bool AreNotificationsEnabled();

} // namespace platform_windows
