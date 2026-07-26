#pragma once

#include <string>

namespace platform_linux {

// Shells out to zenity or kdialog (whichever is installed) for a single
// text-entry prompt - the Linux equivalent of the Windows DialogBoxParam
// prompt, since native tray menus have no free-text item type on any
// platform. Leaves `value` unchanged on Cancel or if neither tool is
// available (the latter is logged, not silently ignored).
void PromptForText(const std::string& title, const std::string& prompt, std::string& value);

} // namespace platform_linux
