#include "ToastPermission.h"
#include "ComInit.h"
#include "StringConvert.h"
#include "core/Log.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>

#include <string_view>

namespace platform_windows {

bool AreNotificationsEnabled() {
    // Idempotent (see ComInit.h) - needed here specifically because this
    // can run on the UI thread via TrayIcon's WM_TIMER handler, which
    // never otherwise touches COM/WinRT directly itself.
    EnsureComInitialized();
    try {
        using namespace winrt::Windows::UI::Notifications;
        // The parameterless overload is documented as unreliable for
        // classic desktop apps - the explicit-AUMID overload is what's
        // actually required here. Must match both
        // SetCurrentProcessExplicitAppUserModelID() in main.cpp and the
        // AppUserModelId set on the Start Menu shortcut in installer.nsi.
        auto notifier = ToastNotificationManager::CreateToastNotifier(L"FeatherRPC.TrayApp");
        return notifier.Setting() == NotificationSetting::Enabled;
    } catch (const winrt::hresult_error& e) {
        // Couldn't determine (e.g. no AppUserModelID registered, or a
        // Windows build old enough not to support this) - default to
        // enabled rather than suppress a real notification based on an
        // inconclusive check. This only runs once (first-run balloon), so
        // unconditional logging is safe.
        core::Log::Write("[warn] Notification-permission check failed (" +
                          NarrowFromWide(std::wstring_view(e.message().c_str(), e.message().size())) +
                          ") - assuming notifications are enabled.");
        return true;
    } catch (...) {
        core::Log::Write("[warn] Notification-permission check failed (unknown error) - assuming notifications are enabled.");
        return true;
    }
}

} // namespace platform_windows
