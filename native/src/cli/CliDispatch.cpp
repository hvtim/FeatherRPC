#include "CliDispatch.h"
#include "StatusFile.h"

#include "core/AppConfig.h"
#include "core/ConfigPaths.h"

#include <charconv>
#include <chrono>
#include <iostream>
#include <thread>

namespace cli {

namespace {

void PrintUsage() {
    std::cout <<
        "Usage: featherrpc <command> [args]\n"
        "\n"
        "  appid get|set <id>\n"
        "  broadcast get|on|off\n"
        "  tracknumber get|on|off\n"
        "  artmode get|auto|custom|off\n"
        "  arturl get|set <url>\n"
        "  icon get|set <key>\n"
        "  pollinterval get|set <ms>\n"
        "  mediasource list|current|get|set <id>\n"
        "  tray get|on|off\n"
        "  status\n"
        "  autostart get|on|off\n"
        "  daemon start|stop|restart\n"
        "  config path\n"
        "  log path\n";
}

core::AppConfig LoadConfig() {
    return core::LoadConfig(core::GetConfigFilePath());
}

void SaveConfig(const core::AppConfig& config) {
    core::SaveConfig(config, core::GetConfigFilePath());
}

// Every `set`-style command ends here: report whether a running instance
// actually picked the change up live, versus just being queued in
// config.json for whenever the app next starts.
void ReportReload(Hooks& hooks) {
    if (hooks.daemonSignal && hooks.daemonSignal->RequestReload()) {
        std::cout << "Saved - applied live.\n";
    } else {
        std::cout << "Saved - not running, will apply next start.\n";
    }
}

// If autostart is already registered, re-registers it so the Startup
// shortcut/LaunchAgent/unit picks up a changed --no-tray argument right
// away, rather than silently launching in the old mode until the user
// happens to re-run `autostart on` themselves.
void RefreshAutostartIfEnabled(Hooks& hooks) {
    if (hooks.autoLaunch && hooks.autoLaunch->IsEnabled()) {
        hooks.autoLaunch->SetEnabled(true);
    }
}

int HandleBoolToggle(const std::vector<std::string>& args, Hooks& hooks,
                      bool core::AppConfig::* field, const char* name) {
    auto config = LoadConfig();
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "get") {
        std::cout << name << ": " << ((config.*field) ? "on" : "off") << "\n";
        return 0;
    }
    if (args[1] == "on" || args[1] == "off") {
        config.*field = (args[1] == "on");
        SaveConfig(config);
        ReportReload(hooks);
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandleStringSet(const std::vector<std::string>& args, Hooks& hooks,
                     std::string core::AppConfig::* field, const char* name) {
    auto config = LoadConfig();
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "get") {
        std::cout << name << ": " << (config.*field) << "\n";
        return 0;
    }
    if (args[1] == "set" && args.size() >= 3) {
        config.*field = args[2];
        SaveConfig(config);
        ReportReload(hooks);
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandleArtMode(const std::vector<std::string>& args, Hooks& hooks) {
    auto config = LoadConfig();
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "get") {
        std::cout << "artmode: " << config.artMode << "\n";
        return 0;
    }
    if (args[1] == "auto" || args[1] == "custom" || args[1] == "off") {
        config.artMode = args[1] == "auto" ? "Auto" : args[1] == "custom" ? "Custom" : "Off";
        SaveConfig(config);
        ReportReload(hooks);
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandlePollInterval(const std::vector<std::string>& args, Hooks& hooks) {
    auto config = LoadConfig();
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "get") {
        std::cout << "pollinterval: " << config.pollIntervalMs << "ms\n";
        return 0;
    }
    if (args[1] == "set" && args.size() >= 3) {
        int ms = 0;
        const std::string& arg = args[2];
        auto result = std::from_chars(arg.data(), arg.data() + arg.size(), ms);
        bool valid = result.ec == std::errc() && result.ptr == arg.data() + arg.size() && ms > 0;
        if (!valid) {
            std::cout << "pollinterval must be a positive number of milliseconds.\n";
            return 1;
        }
        config.pollIntervalMs = ms;
        SaveConfig(config);
        ReportReload(hooks);
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandleMediaSource(const std::vector<std::string>& args, Hooks& hooks) {
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "list") {
        if (!hooks.listMediaSources) {
            std::cout << "No live media source enumeration on this platform.\n";
            return 0;
        }
        for (const auto& source : hooks.listMediaSources()) {
            std::cout << source.id << "\t" << source.displayName << "\n";
        }
        return 0;
    }
    if (args[1] == "current") {
        if (!hooks.currentMediaSource) {
            std::cout << "Not available on this platform/source.\n";
            return 0;
        }
        auto current = hooks.currentMediaSource();
        if (!current.has_value()) {
            std::cout << "Nothing playing right now.\n";
            return 0;
        }
        std::cout << *current << "\n";
        return 0;
    }
    auto config = LoadConfig();
    if (args[1] == "get") {
        std::cout << "mediasource: " << config.mediaSource << "\n";
        return 0;
    }
    if (args[1] == "set" && args.size() >= 3) {
        config.mediaSource = args[2];
        SaveConfig(config);
        ReportReload(hooks);
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandleTray(const std::vector<std::string>& args, Hooks& hooks) {
    auto config = LoadConfig();
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "get") {
        std::cout << "tray: " << (config.trayEnabled ? "on" : "off") << "\n";
        return 0;
    }
    if (args[1] == "on" || args[1] == "off") {
        config.trayEnabled = (args[1] == "on");
        SaveConfig(config);
        RefreshAutostartIfEnabled(hooks);
        std::cout << "Saved - takes effect next launch (a running instance can't add/remove its own tray icon).\n";
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandleStatus(Hooks& hooks) {
    auto contents = ReadStatusFile();
    bool running = hooks.daemonSignal && hooks.daemonSignal->IsRunning();
    if (!contents) {
        std::cout << (running ? "Running - no status reported yet.\n" : "Not running.\n");
        return 0;
    }
    if (running) {
        std::cout << contents->status << " (pid " << contents->pid << ")\n";
    } else {
        std::cout << "Not running (stale status from a previous run: " << contents->status << ")\n";
    }
    return 0;
}

int HandleAutostart(const std::vector<std::string>& args, Hooks& hooks) {
    if (!hooks.autoLaunch) {
        std::cout << "Autostart isn't available on this platform.\n";
        return 1;
    }
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }
    if (args[1] == "get") {
        std::cout << "autostart: " << (hooks.autoLaunch->IsEnabled() ? "on" : "off") << "\n";
        return 0;
    }
    if (args[1] == "on" || args[1] == "off") {
        hooks.autoLaunch->SetEnabled(args[1] == "on");
        std::cout << "autostart: " << args[1] << "\n";
        return 0;
    }
    PrintUsage();
    return 1;
}

int HandleDaemon(const std::vector<std::string>& args, Hooks& hooks) {
    if (args.size() < 2) {
        PrintUsage();
        return 1;
    }

    auto waitUntil = [&](bool running, int maxChecks) {
        for (int i = 0; i < maxChecks; ++i) {
            if (hooks.daemonSignal->IsRunning() == running) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return false;
    };

    if (args[1] == "start") {
        if (hooks.daemonSignal->IsRunning()) {
            std::cout << "Already running.\n";
            return 0;
        }
        if (!hooks.spawnDaemon || !hooks.spawnDaemon()) {
            std::cout << "Failed to start.\n";
            return 1;
        }
        std::cout << (waitUntil(true, 10) ? "Started.\n" : "Started, but not confirmed running yet.\n");
        return 0;
    }
    if (args[1] == "stop") {
        if (!hooks.daemonSignal->IsRunning()) {
            std::cout << "Not running.\n";
            return 0;
        }
        hooks.daemonSignal->RequestQuit();
        std::cout << (waitUntil(false, 15) ? "Stopped.\n" : "Requested stop, but it's still running.\n");
        return 0;
    }
    if (args[1] == "restart") {
        if (hooks.daemonSignal->IsRunning()) {
            hooks.daemonSignal->RequestQuit();
            waitUntil(false, 15);
        }
        if (!hooks.spawnDaemon || !hooks.spawnDaemon()) {
            std::cout << "Failed to start.\n";
            return 1;
        }
        std::cout << (waitUntil(true, 10) ? "Restarted.\n" : "Started, but not confirmed running yet.\n");
        return 0;
    }
    PrintUsage();
    return 1;
}

} // namespace

int Run(const std::vector<std::string>& args, Hooks& hooks) {
    if (args.empty()) {
        PrintUsage();
        return 1;
    }

    const std::string& command = args[0];
    if (command == "appid") {
        return HandleStringSet(args, hooks, &core::AppConfig::clientId, "appid");
    }
    if (command == "broadcast") {
        return HandleBoolToggle(args, hooks, &core::AppConfig::broadcastEnabled, "broadcast");
    }
    if (command == "tracknumber") {
        return HandleBoolToggle(args, hooks, &core::AppConfig::showTrackNumber, "tracknumber");
    }
    if (command == "artmode") {
        return HandleArtMode(args, hooks);
    }
    if (command == "arturl") {
        return HandleStringSet(args, hooks, &core::AppConfig::customArtUrl, "arturl");
    }
    if (command == "icon") {
        return HandleStringSet(args, hooks, &core::AppConfig::largeImageKey, "icon");
    }
    if (command == "pollinterval") {
        return HandlePollInterval(args, hooks);
    }
    if (command == "mediasource") {
        return HandleMediaSource(args, hooks);
    }
    if (command == "tray") {
        return HandleTray(args, hooks);
    }
    if (command == "status") {
        return HandleStatus(hooks);
    }
    if (command == "autostart") {
        return HandleAutostart(args, hooks);
    }
    if (command == "daemon") {
        return HandleDaemon(args, hooks);
    }
    if (command == "config" && args.size() >= 2 && args[1] == "path") {
        std::cout << core::GetConfigFilePath().string() << "\n";
        return 0;
    }
    if (command == "log" && args.size() >= 2 && args[1] == "path") {
        std::cout << core::GetLogFilePath().string() << "\n";
        return 0;
    }

    PrintUsage();
    return 1;
}

} // namespace cli
