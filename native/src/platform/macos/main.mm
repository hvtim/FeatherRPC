#include "core/AppConfig.h"
#include "core/ConfigPaths.h"
#include "core/Log.h"
#include "core/PresenceEngine.h"

#include "AppleSearchAlbumArtLookup.h"
#include "LaunchAgentAutoLaunch.h"
#include "MusicMediaSource.h"
#include "StatusItemTray.h"
#include "TextPrompt.h"
#include "cli/StatusFile.h"
#include "platform/posix/DaemonSignal.h"
#include "platform/posix/UnixSocketIpcTransport.h"

#import <Cocoa/Cocoa.h>

#include <memory>
#include <string>

int main(int argc, const char** argv) {
    bool noTray = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-tray") {
            noTray = true;
        }
    }

    @autoreleasepool {
        core::Log::Init(core::GetLogFilePath());
        core::Log::Write("FeatherRPC starting...");

        // First thing, before touching config/engine/tray - refuses to
        // start a second instance (tray or headless) alongside one
        // that's already running, no matter what launched it (a
        // LaunchAgent, a manual second run, or `featherrpc-cli daemon
        // start`).
        auto singleInstanceLock = platform_posix::SingleInstanceLock::TryAcquire();
        if (!singleInstanceLock.Acquired()) {
            core::Log::Write("[error] FeatherRPC is already running - exiting.");
            // 75, not 1 - same convention as the Linux build (see
            // featherrpc.service's RestartPreventExitStatus=75), so any
            // exit code inspection stays consistent across platforms.
            return 75;
        }

        core::AppConfig config = core::LoadConfig(core::GetConfigFilePath());

        platform_macos::LaunchAgentAutoLaunch autoLaunch;

        core::PresenceEngine engine(
            config,
            std::make_unique<platform_macos::MusicMediaSource>(),
            std::make_unique<platform_macos::AppleSearchAlbumArtLookup>(),
            [] { return std::make_unique<platform_posix::UnixSocketIpcTransport>(); });

        if (noTray) {
            // No NSApplication/StatusItemTray at all in this path - the
            // sigwait loop below is the entire event loop, replacing the
            // tray's Cocoa run loop the same way the Linux daemon path
            // replaces AppIndicator's glib loop.
            platform_posix::DaemonBlockSignalsAndWritePidFile();

            engine.OnStatusChanged = [&] {
                cli::WriteStatusFile(engine.Status());
            };

            engine.Start();
            core::Log::Write("Running headless (--no-tray).");

            for (;;) {
                auto signal = platform_posix::DaemonWaitForSignal();
                if (signal == platform_posix::DaemonSignalKind::Reload) {
                    // Media source never changes in this phase - Music.app
                    // only - same as the tray path's OnConfigChanged.
                    core::AppConfig reloaded = core::LoadConfig(core::GetConfigFilePath());
                    engine.UpdateConfig(reloaded, nullptr);
                    core::Log::Write("Reloaded config.");
                } else {
                    break;
                }
            }

            engine.Stop();
            platform_posix::DaemonRemovePidFile();
            core::Log::Write("Exiting.");
            return 0;
        }

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
            if (newConfig.trayEnabled != config.trayEnabled) {
                core::Log::Write("Tray icon preference changed - takes effect next launch.");
            }
            config = newConfig;
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

        tray.OnEditFallbackImageKey = [&](std::string& value) {
            nativeui::PromptForText("FeatherRPC", "Fallback image asset key:", value);
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
