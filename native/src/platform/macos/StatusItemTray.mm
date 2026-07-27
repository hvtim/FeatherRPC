#include "StatusItemTray.h"
#include "MediaRemoteSource.h"

#import <Cocoa/Cocoa.h>

#include <vector>

@interface ITRPCMenuTarget : NSObject
@property (nonatomic, assign) nativeui::StatusItemTray* tray;
- (void)menuAction:(id)sender;
@end

@implementation ITRPCMenuTarget
- (void)menuAction:(id)sender {
    NSMenuItem* item = (NSMenuItem*)sender;
    if (_tray) {
        _tray->HandleCommand(static_cast<int>(item.tag));
    }
}
@end

namespace nativeui {

namespace {

constexpr int kCmdSetAppId = 1;
constexpr int kCmdToggleBroadcast = 3;
constexpr int kCmdToggleTrackNumber = 4;
constexpr int kCmdToggleStartAtLogin = 5;
constexpr int kCmdExit = 6;
constexpr int kCmdToggleTrayIcon = 7;
constexpr int kCmdSetFallbackKey = 8;
constexpr int kCmdArtModeAuto = 200;
constexpr int kCmdArtModeCustom = 201;
constexpr int kCmdArtModeOff = 202;
constexpr int kCmdPollIntervalBase = 300;
constexpr int kCmdMediaSourceBase = 400; // + index into GetAvailableSources()

const std::vector<int>& PollIntervalPresetsMs() {
    static const std::vector<int> presets = {1000, 2000, 5000, 10000};
    return presets;
}

} // namespace

struct StatusItemTray::Impl {
    NSStatusItem* statusItem = nil;
    ITRPCMenuTarget* target = nil;
};

StatusItemTray::StatusItemTray() : _impl(std::make_unique<Impl>()) {}

StatusItemTray::~StatusItemTray() {
    if (_impl->statusItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:_impl->statusItem];
    }
}

bool StatusItemTray::Create() {
    _impl->target = [[ITRPCMenuTarget alloc] init];
    _impl->target.tray = this;

    _impl->statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    // Placeholder glyph - swap for the real icon.icns asset once the .app
    // bundle's Resources are wired up (statusItem.button.image, not .title).
    _impl->statusItem.button.title = @"\U0001F3B5";
    _impl->statusItem.button.toolTip = @"FeatherRPC";

    RebuildMenu();
    return _impl->statusItem != nil;
}

void StatusItemTray::SetInitialState(const core::AppConfig& config, bool startAtLogin) {
    _config = config;
    _startAtLogin = startAtLogin;
    if (_impl->statusItem) {
        RebuildMenu();
    }
}

void StatusItemTray::PostStatusUpdate(const std::string& text) {
    __block std::string copy = text;
    __block Impl* impl = _impl.get();
    dispatch_async(dispatch_get_main_queue(), ^{
        if (impl->statusItem) {
            impl->statusItem.button.toolTip = [NSString stringWithUTF8String:("FeatherRPC - " + copy).c_str()];
        }
    });
}

int StatusItemTray::RunMessageLoop() {
    [NSApp run];
    return 0;
}

void StatusItemTray::RebuildMenu() {
    NSMenu* menu = [[NSMenu alloc] init];

    auto addItem = [&](NSString* title, int tag, bool checked) -> NSMenuItem* {
        NSMenuItem* item = [menu addItemWithTitle:title action:@selector(menuAction:) keyEquivalent:@""];
        item.target = _impl->target;
        item.tag = tag;
        item.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
        return item;
    };

    addItem(@"Set Discord Application ID...", kCmdSetAppId, false);

    // Unlike Windows/Linux, the list here is fixed (Music.app's own
    // source, plus the MediaRemote-adapter-backed "any app" source) -
    // there's nothing to dynamically re-enumerate, so it's rebuilt
    // straight from GetAvailableSources() every time rather than needing
    // an OnRefreshMediaSources-style hook.
    _mediaSources = platform_macos::MediaRemoteSource::GetAvailableSources();
    NSMenu* sourceMenu = [[NSMenu alloc] init];
    NSMenuItem* sourceParent = [menu addItemWithTitle:@"Media Source" action:nil keyEquivalent:@""];
    sourceParent.submenu = sourceMenu;
    for (size_t i = 0; i < _mediaSources.size(); ++i) {
        NSString* title = [NSString stringWithUTF8String:_mediaSources[i].displayName.c_str()];
        NSMenuItem* item = [sourceMenu addItemWithTitle:title action:@selector(menuAction:) keyEquivalent:@""];
        item.target = _impl->target;
        item.tag = kCmdMediaSourceBase + static_cast<int>(i);
        bool selected = _mediaSources[i].id == _config.mediaSource
            || (_mediaSources[i].id == "Music" && _config.mediaSource != "MediaRemote");
        item.state = selected ? NSControlStateValueOn : NSControlStateValueOff;
    }

    [menu addItem:[NSMenuItem separatorItem]];

    addItem(@"Broadcast now playing to Discord", kCmdToggleBroadcast, _config.broadcastEnabled);
    addItem(@"Show track number", kCmdToggleTrackNumber, _config.showTrackNumber);

    NSMenu* artMenu = [[NSMenu alloc] init];
    NSMenuItem* artParent = [menu addItemWithTitle:@"Album Art" action:nil keyEquivalent:@""];
    artParent.submenu = artMenu;
    auto addArtItem = [&](NSString* title, int tag, const std::string& mode) {
        NSMenuItem* item = [artMenu addItemWithTitle:title action:@selector(menuAction:) keyEquivalent:@""];
        item.target = _impl->target;
        item.tag = tag;
        item.state = (_config.artMode == mode) ? NSControlStateValueOn : NSControlStateValueOff;
    };
    addArtItem(@"Automatic (look up cover art)", kCmdArtModeAuto, "Auto");
    addArtItem(@"Custom image URL...", kCmdArtModeCustom, "Custom");
    addArtItem(@"Fallback image only", kCmdArtModeOff, "Off");
    NSMenuItem* setFallbackKeyItem = [artMenu addItemWithTitle:@"Set Fallback Image Key..."
                                                          action:@selector(menuAction:)
                                                   keyEquivalent:@""];
    setFallbackKeyItem.target = _impl->target;
    setFallbackKeyItem.tag = kCmdSetFallbackKey;

    NSMenu* pollMenu = [[NSMenu alloc] init];
    NSMenuItem* pollParent = [menu addItemWithTitle:@"Poll Interval" action:nil keyEquivalent:@""];
    pollParent.submenu = pollMenu;
    const auto& presets = PollIntervalPresetsMs();
    for (size_t i = 0; i < presets.size(); ++i) {
        NSString* title = [NSString stringWithFormat:@"%lds", (long)(presets[i] / 1000)];
        NSMenuItem* item = [pollMenu addItemWithTitle:title action:@selector(menuAction:) keyEquivalent:@""];
        item.target = _impl->target;
        item.tag = kCmdPollIntervalBase + static_cast<int>(i);
        item.state = (presets[i] == _config.pollIntervalMs) ? NSControlStateValueOn : NSControlStateValueOff;
    }

    [menu addItem:[NSMenuItem separatorItem]];
    addItem(@"Start automatically when you log in", kCmdToggleStartAtLogin, _startAtLogin);
    // Labeled/checked as the positive ("Show"), not "Disable", so the
    // checkmark reads naturally - checked means the icon is showing.
    // Can't apply live (a running process can't drop its own status
    // item mid-session), so this only takes effect on the next launch.
    addItem(@"Show tray icon", kCmdToggleTrayIcon, _config.trayEnabled);

    [menu addItem:[NSMenuItem separatorItem]];
    addItem(@"Exit", kCmdExit, false);

    _impl->statusItem.menu = menu;
}

void StatusItemTray::HandleCommand(int commandId) {
    if (commandId == kCmdExit) {
        [NSApp stop:nil];
        // -stop: only breaks the run loop on the next event cycle - post a
        // dummy event so it actually wakes up and exits immediately rather
        // than waiting for the next real UI event.
        NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                             location:NSZeroPoint
                                        modifierFlags:0
                                            timestamp:0
                                         windowNumber:0
                                              context:nil
                                              subtype:0
                                                data1:0
                                                data2:0];
        [NSApp postEvent:event atStart:true];
        return;
    }

    if (commandId == kCmdSetAppId) {
        std::string value = _config.clientId;
        std::string original = value;
        if (OnEditApplicationId) {
            OnEditApplicationId(value);
        }
        if (value != original) {
            _config.clientId = value;
            NotifyConfigChanged();
        }
        return;
    }

    if (commandId == kCmdArtModeCustom) {
        // Picking "Custom image URL..." both selects Custom mode and
        // immediately prompts for the URL - one action instead of two.
        _config.artMode = "Custom";
        std::string value = _config.customArtUrl;
        if (OnEditCustomArtUrl) {
            OnEditCustomArtUrl(value);
        }
        _config.customArtUrl = value;
        NotifyConfigChanged();
        return;
    }

    if (commandId == kCmdSetFallbackKey) {
        std::string value = _config.largeImageKey;
        std::string original = value;
        if (OnEditFallbackImageKey) {
            OnEditFallbackImageKey(value);
        }
        if (value != original) {
            _config.largeImageKey = value;
            NotifyConfigChanged();
        }
        return;
    }

    if (commandId == kCmdToggleBroadcast) {
        _config.broadcastEnabled = !_config.broadcastEnabled;
        NotifyConfigChanged();
        return;
    }

    if (commandId == kCmdToggleTrackNumber) {
        _config.showTrackNumber = !_config.showTrackNumber;
        NotifyConfigChanged();
        return;
    }

    if (commandId == kCmdToggleStartAtLogin) {
        _startAtLogin = !_startAtLogin;
        if (OnStartAtLoginChanged) {
            OnStartAtLoginChanged(_startAtLogin);
        }
        RebuildMenu();
        return;
    }

    if (commandId == kCmdToggleTrayIcon) {
        _config.trayEnabled = !_config.trayEnabled;
        NotifyConfigChanged();
        return;
    }

    if (commandId == kCmdArtModeAuto) {
        _config.artMode = "Auto";
        NotifyConfigChanged();
        return;
    }

    if (commandId == kCmdArtModeOff) {
        _config.artMode = "Off";
        NotifyConfigChanged();
        return;
    }

    if (commandId >= kCmdPollIntervalBase
        && commandId < kCmdPollIntervalBase + static_cast<int>(PollIntervalPresetsMs().size())) {
        _config.pollIntervalMs = PollIntervalPresetsMs()[commandId - kCmdPollIntervalBase];
        NotifyConfigChanged();
        return;
    }

    if (commandId >= kCmdMediaSourceBase
        && commandId < kCmdMediaSourceBase + static_cast<int>(_mediaSources.size())) {
        _config.mediaSource = _mediaSources[commandId - kCmdMediaSourceBase].id;
        NotifyConfigChanged();
        return;
    }
}

void StatusItemTray::NotifyConfigChanged() {
    // Unlike Windows' TrackPopupMenu (rebuilt fresh every time it's shown),
    // NSMenu is a persistent object - it needs an explicit rebuild to pick
    // up new checkmarks after any change.
    RebuildMenu();
    if (OnConfigChanged) {
        OnConfigChanged(_config);
    }
}

} // namespace nativeui
