#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"

#include "AppleSearchAlbumArtLookup.h"
#include "LaunchAgentAutoLaunch.h"
#include "MusicMediaSource.h"
#include "StatusItemTray.h"
#include "TextPrompt.h"
#include "platform/posix/UnixSocketIpcTransport.h"

#import <Cocoa/Cocoa.h>

#include <memory>
#include <string>

int main() {
    @autoreleasepool {
        core::Log::Init(core::GetLogFilePath());
        core::Log::Write("FeatherRPC starting...");

        core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());

        platform_macos::LaunchAgentAutoLaunch autoLaunch;

        core::PresenceEngine engine(
            config,
            std::make_unique<platform_macos::MusicMediaSource>(),
            std::make_unique<platform_macos::AppleSearchAlbumArtLookup>(),
            [] { return std::make_unique<platform_posix::UnixSocketIpcTransport>(); });

        [NSApplication sharedApplication];
        // .accessory: no Dock icon, no app menu bar - tray-icon-only
        // footprint, matching the Windows build's hidden-window approach.
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        nativeui::StatusItemTray tray;
        if (!tray.Create()) {
            core::Log::Write("Failed to create the status item.");
            return 1;
        }
        tray.SetInitialState(config, autoLaunch.IsEnabled());

        tray.OnConfigChanged = [&](const core::AppConfig& newConfig) {
            core::SaveConfig(newConfig, core::GetConfigFilePath());
            // Media source never changes in this phase - Music.app only.
            engine.UpdateConfig(newConfig, nullptr);
        };

        tray.OnStartAtLoginChanged = [&](bool enabled) {
            autoLaunch.SetEnabled(enabled);
        };

        tray.OnEditApplicationId = [&](std::string& value) {
            nativeui::PromptForText("FeatherRPC", "Discord Application ID:", value);
        };

        tray.OnEditCustomArtUrl = [&](std::string& value) {
            nativeui::PromptForText("FeatherRPC", "Image URL (512x512 recommended):", value);
        };

        engine.OnStatusChanged = [&] {
            tray.PostStatusUpdate(engine.Status());
        };

        engine.Start();
        int exitCode = tray.RunMessageLoop();
        engine.Stop();

        core::Log::Write("Exiting.");
        return exitCode;
    }
}
