#include "AppIndicatorTray.h"
#include "core/Log.h"

#include <ayatana-appindicator.h>

namespace nativeui {

namespace {

constexpr const char* kAppId = "featherrpc";

// GMenuModel-exported menus have no local widget and thus no "about to
// show" signal to hook a just-in-time refresh into (unlike the GTK3
// variant's GtkMenu) - the whole menu/action-group pair is rebuilt from
// config_ on this timer instead and reassigned to the indicator. A
// shorter interval would make a newly-launched MPRIS player show up in
// the Media Source submenu faster, at the cost of a real, unverified risk:
// reassigning the menu while it happens to be open on screen might cause a
// visible flicker or force it closed - not something that can be checked
// without a real desktop session.
constexpr unsigned kMenuRefreshIntervalSeconds = 10;

struct StatusUpdatePayload {
    AppIndicatorTray* tray;
    std::string text;
};

} // namespace

AppIndicatorTray::AppIndicatorTray() = default;

AppIndicatorTray::~AppIndicatorTray() {
    if (indicator_) {
        g_object_unref(indicator_);
    }
}

bool AppIndicatorTray::Create(const std::string& iconName) {
    indicator_ = app_indicator_new(kAppId, iconName.c_str(), APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    if (!indicator_) {
        return false;
    }
    app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);
    WarnIfNoTrayHost();

    RebuildMenu();
    g_timeout_add_seconds(kMenuRefreshIntervalSeconds, RefreshMenuTimerThunk, this);

    return true;
}

void AppIndicatorTray::SetInitialState(const core::AppConfig& config, bool startAtLogin) {
    config_ = config;
    startAtLogin_ = startAtLogin;
}

void AppIndicatorTray::PostStatusUpdate(const std::string& text) {
    // Ownership transfers to whichever main-loop iteration handles the
    // idle callback - freed there, mirroring the Windows tray's
    // heap-allocated PostMessage payload.
    auto* payload = new StatusUpdatePayload{this, text};
    g_idle_add(ApplyStatusUpdateThunk, payload);
}

int AppIndicatorTray::RunMessageLoop() {
    loop_ = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop_);
    g_main_loop_unref(loop_);
    loop_ = nullptr;
    return 0;
}

gboolean AppIndicatorTray::ApplyStatusUpdateThunk(gpointer data) {
    auto* payload = static_cast<StatusUpdatePayload*>(data);
    payload->tray->ApplyStatusUpdateNow(payload->text);
    delete payload;
    return G_SOURCE_REMOVE;
}

void AppIndicatorTray::ApplyStatusUpdateNow(const std::string& text) {
    // AppIndicator's "title" is the closest cross-desktop analog to the
    // Windows tray tooltip - how (or whether) each desktop surfaces it is
    // shell-dependent and unverified without a real desktop session.
    app_indicator_set_title(indicator_, text.c_str());
}

gboolean AppIndicatorTray::RefreshMenuTimerThunk(gpointer data) {
    static_cast<AppIndicatorTray*>(data)->RebuildMenu();
    return G_SOURCE_CONTINUE;
}

void AppIndicatorTray::OnActivateThunk(GSimpleAction* action, GVariant* parameter, gpointer data) {
    static_cast<AppIndicatorTray*>(data)->HandleActivate(action, parameter);
}

void AppIndicatorTray::HandleActivate(GSimpleAction* action, GVariant* parameter) {
    const char* nameC = g_action_get_name(G_ACTION(action));
    std::string name = nameC ? nameC : "";

    if (name == "set-app-id") {
        std::string value = config_.clientId;
        std::string original = value;
        if (OnEditApplicationId) {
            OnEditApplicationId(value);
        }
        if (value != original) {
            config_.clientId = value;
            NotifyConfigChanged();
        }
        return;
    }

    if (name == "exit") {
        if (loop_) {
            g_main_loop_quit(loop_);
        }
        return;
    }

    // Stateful boolean toggles carry no target parameter - read the
    // current state, flip it, and commit via set_state (mirroring the
    // library's own onCheckActivate example rather than waiting on a
    // separate "change-state" round trip).
    if (name == "broadcast" || name == "track-number" || name == "start-at-login") {
        GVariant* state = g_action_get_state(G_ACTION(action));
        bool active = !g_variant_get_boolean(state);
        g_variant_unref(state);
        g_simple_action_set_state(action, g_variant_new_boolean(active));

        if (name == "broadcast") {
            config_.broadcastEnabled = active;
            NotifyConfigChanged();
        } else if (name == "track-number") {
            config_.showTrackNumber = active;
            NotifyConfigChanged();
        } else {
            startAtLogin_ = active;
            if (OnStartAtLoginChanged) {
                OnStartAtLoginChanged(active);
            }
        }
        return;
    }

    if (name == "art-mode" && parameter) {
        std::string mode = g_variant_get_string(parameter, nullptr);
        g_simple_action_set_state(action, g_variant_new_string(mode.c_str()));
        config_.artMode = mode;
        if (mode == "Custom") {
            // Picking "Custom image URL..." both selects Custom mode and
            // immediately prompts for the URL - one action instead of two.
            std::string url = config_.customArtUrl;
            if (OnEditCustomArtUrl) {
                OnEditCustomArtUrl(url);
            }
            config_.customArtUrl = url;
        }
        NotifyConfigChanged();
        return;
    }

    if (name == "media-source" && parameter) {
        std::string id = g_variant_get_string(parameter, nullptr);
        g_simple_action_set_state(action, g_variant_new_string(id.c_str()));
        config_.mediaSource = id;
        NotifyConfigChanged();
        return;
    }

    if (name == "poll-interval" && parameter) {
        int ms = g_variant_get_int32(parameter);
        g_simple_action_set_state(action, g_variant_new_int32(ms));
        config_.pollIntervalMs = ms;
        NotifyConfigChanged();
        return;
    }
}

void AppIndicatorTray::NotifyConfigChanged() {
    if (OnConfigChanged) {
        OnConfigChanged(config_);
    }
}

void AppIndicatorTray::RebuildMenu() {
    if (OnRefreshMediaSources) {
        mediaSources_ = OnRefreshMediaSources();
    }

    GMenu* menu = g_menu_new();
    GSimpleActionGroup* actions = g_simple_action_group_new();

    // "indicator." matches the namespace libayatana-appindicator-glib's
    // own example uses for every action reference - not derived
    // automatically, must match on both sides (the menu item's detailed
    // action string here, and the action's own registered name below).
    auto addAction = [&](const char* name, GVariant* initialState, const GVariantType* paramType) {
        GSimpleAction* action = initialState ? g_simple_action_new_stateful(name, paramType, initialState)
                                              : g_simple_action_new(name, paramType);
        g_signal_connect(action, "activate", G_CALLBACK(OnActivateThunk), this);
        g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(action));
        g_object_unref(action);
    };

    addAction("set-app-id", nullptr, nullptr);
    addAction("media-source", g_variant_new_string(config_.mediaSource.c_str()), G_VARIANT_TYPE_STRING);
    addAction("broadcast", g_variant_new_boolean(config_.broadcastEnabled), nullptr);
    addAction("track-number", g_variant_new_boolean(config_.showTrackNumber), nullptr);
    addAction("art-mode", g_variant_new_string(config_.artMode.c_str()), G_VARIANT_TYPE_STRING);
    addAction("poll-interval", g_variant_new_int32(config_.pollIntervalMs), G_VARIANT_TYPE_INT32);
    addAction("start-at-login", g_variant_new_boolean(startAtLogin_), nullptr);
    addAction("exit", nullptr, nullptr);

    GMenuItem* setAppIdItem = g_menu_item_new("Set Discord Application ID...", "indicator.set-app-id");
    g_menu_append_item(menu, setAppIdItem);
    g_object_unref(setAppIdItem);

    // Unlike Windows (which always has "iTunes" as a fixed first entry),
    // Linux has no built-in source: the list is purely whatever MPRIS
    // players OnRefreshMediaSources found on this rebuild.
    GMenu* sourceSubmenu = g_menu_new();
    for (const auto& source : mediaSources_) {
        GMenuItem* item = g_menu_item_new(source.displayName.c_str(), nullptr);
        g_menu_item_set_action_and_target_value(item, "indicator.media-source", g_variant_new_string(source.id.c_str()));
        g_menu_append_item(sourceSubmenu, item);
        g_object_unref(item);
    }
    GMenuItem* sourceItem = g_menu_item_new_submenu("Media Source", G_MENU_MODEL(sourceSubmenu));
    g_menu_append_item(menu, sourceItem);
    g_object_unref(sourceItem);
    g_object_unref(sourceSubmenu);

    GMenu* toggleSection = g_menu_new();
    g_menu_append(toggleSection, "Broadcast now playing to Discord", "indicator.broadcast");
    g_menu_append(toggleSection, "Show track number", "indicator.track-number");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(toggleSection));
    g_object_unref(toggleSection);

    GMenu* artSubmenu = g_menu_new();
    auto addArtItem = [&](const char* label, const char* mode) {
        GMenuItem* item = g_menu_item_new(label, nullptr);
        g_menu_item_set_action_and_target_value(item, "indicator.art-mode", g_variant_new_string(mode));
        g_menu_append_item(artSubmenu, item);
        g_object_unref(item);
    };
    addArtItem("Automatic (look up cover art)", "Auto");
    addArtItem("Custom image URL...", "Custom");
    addArtItem("Static logo only", "Off");
    GMenuItem* artMenuItem = g_menu_item_new_submenu("Album Art", G_MENU_MODEL(artSubmenu));
    g_menu_append_item(menu, artMenuItem);
    g_object_unref(artMenuItem);
    g_object_unref(artSubmenu);

    GMenu* pollSubmenu = g_menu_new();
    for (int ms : pollIntervalPresetsMs_) {
        std::string label = std::to_string(ms / 1000) + "s";
        GMenuItem* item = g_menu_item_new(label.c_str(), nullptr);
        g_menu_item_set_action_and_target_value(item, "indicator.poll-interval", g_variant_new_int32(ms));
        g_menu_append_item(pollSubmenu, item);
        g_object_unref(item);
    }
    GMenuItem* pollMenuItem = g_menu_item_new_submenu("Poll Interval", G_MENU_MODEL(pollSubmenu));
    g_menu_append_item(menu, pollMenuItem);
    g_object_unref(pollMenuItem);
    g_object_unref(pollSubmenu);

    GMenu* loginSection = g_menu_new();
    g_menu_append(loginSection, "Start automatically when you log in", "indicator.start-at-login");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(loginSection));
    g_object_unref(loginSection);

    GMenu* exitSection = g_menu_new();
    g_menu_append(exitSection, "Exit", "indicator.exit");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(exitSection));
    g_object_unref(exitSection);

    // set_menu/set_actions each take their own reference (matching the
    // library's own example, which unrefs its local pMenu/pActions
    // immediately after) - nothing here needs to outlive this function.
    app_indicator_set_menu(indicator_, menu);
    g_object_unref(menu);
    app_indicator_set_actions(indicator_, actions);
    g_object_unref(actions);
}

void AppIndicatorTray::WarnIfNoTrayHost() {
    // Vanilla GNOME (Fedora Workstation, stock Ubuntu, etc.) ships no
    // StatusNotifierItem host at all - nothing can make a tray icon
    // appear there regardless of backend, so the right fix is telling the
    // user why, not silently doing nothing (see the plan's Linux tray
    // test matrix).
    GError* error = nullptr;
    GDBusConnection* conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!conn) {
        g_clear_error(&error);
        return;
    }

    GVariant* result = g_dbus_connection_call_sync(conn, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", "org.kde.StatusNotifierWatcher"),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    g_object_unref(conn);
    if (!result) {
        g_clear_error(&error);
        return;
    }

    gboolean hasOwner = FALSE;
    g_variant_get(result, "(b)", &hasOwner);
    g_variant_unref(result);

    if (!hasOwner) {
        core::Log::Write(
            "[warn] No StatusNotifierWatcher is running, so the tray icon will not be visible on this desktop - "
            "on GNOME, install the \"AppIndicator and KStatusNotifierItem Support\" extension to fix this. "
            "FeatherRPC will keep running and updating Discord regardless; edit config.json directly if you "
            "need to change settings without the tray menu.");
    }
}

} // namespace nativeui
