#include "TextPrompt.h"

#import <Cocoa/Cocoa.h>

namespace nativeui {

bool PromptForText(const std::string& title, const std::string& label, std::string& value) {
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = [NSString stringWithUTF8String:title.c_str()];
        alert.informativeText = [NSString stringWithUTF8String:label.c_str()];
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Cancel"];

        NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)];
        field.stringValue = [NSString stringWithUTF8String:value.c_str()];
        alert.accessoryView = field;
        [alert.window setInitialFirstResponder:field];

        // The app has no Dock icon/regular window (NSApplicationActivationPolicyAccessory,
        // set in main.mm), so without this the alert can open behind
        // whatever app currently has focus.
        [NSApp activateIgnoringOtherApps:YES];

        NSModalResponse response = [alert runModal];
        if (response == NSAlertFirstButtonReturn) {
            value = field.stringValue.UTF8String ? field.stringValue.UTF8String : "";
            return true;
        }
        return false;
    }
}

} // namespace nativeui
