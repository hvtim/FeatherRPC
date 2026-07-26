#pragma once

#include <windows.h>
#include <string>

namespace nativeui {

// Tiny native "prompt for text" dialog - one edit field + OK/Cancel, built
// with DialogBoxParam against the template in resources.rc. This is the
// one piece of real UI this app needs beyond menu items, per the plan's
// "no GUI toolkit at all" UI Layer decision: native popup menus have no
// text-entry item type on any platform, so free-text fields (Application
// ID, custom art URL) need this instead.
//
// Returns true and writes the entered text to `value` if the user pressed
// OK; returns false (leaving `value` unchanged) on Cancel or window close.
bool PromptForText(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value);

} // namespace nativeui
