#pragma once

#include <string>

namespace nativeui {

// NSAlert + an accessory NSTextField - the macOS equivalent of the Windows
// build's DialogBoxParam text-prompt dialog. Must be called on the main
// thread (NSAlert requirement) - true here since StatusItemTray only
// invokes this from its own menu-action callbacks, which Cocoa always
// dispatches on the main thread.
//
// Returns true and writes the entered text to `value` if the user pressed
// OK; returns false (leaving `value` unchanged) on Cancel.
bool PromptForText(const std::string& title, const std::string& label, std::string& value);

} // namespace nativeui
