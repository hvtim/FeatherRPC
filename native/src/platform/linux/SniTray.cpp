#include "SniTray.h"
#include "core/Log.h"

#include <gio/gio.h>

#include <string>

namespace nativeui {

namespace {

constexpr const char* kAppId = "featherrpc";
constexpr const char* kSniObjectPath = "/StatusNotifierItem";
constexpr const char* kMenuObjectPath = "/MenuBar";
constexpr const char* kSniInterfaceName = "org.kde.StatusNotifierItem";
constexpr const char* kMenuInterfaceName = "com.canonical.dbusmenu";

// Minimal but spec-complete introspection XML for the properties/methods/
// signals this class actually implements. Optional StatusNotifierItem
// properties not implemented here (WindowId, AttentionIconName,
// OverlayIconName, AttentionMovieName, IconPixmap) are genuinely optional
// per the freedesktop/KDE spec - hosts fall back gracefully when they're
// absent, the same way they did when AppIndicator/libayatana-appindicator
// didn't set them either.
// Custom "XML" raw-string delimiter, not the default R"( )" - several
// D-Bus type signatures below end with ")" right before the attribute's
// closing quote (e.g. type="(sa(iiay)ss)"), which would collide with and
// prematurely close a default-delimited raw string.
constexpr const char* kSniIntrospectionXml = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Scroll">
      <arg name="delta" type="i" direction="in"/>
      <arg name="orientation" type="s" direction="in"/>
    </method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus">
      <arg name="status" type="s"/>
    </signal>
  </interface>
</node>
)XML";

constexpr const char* kMenuIntrospectionXml = R"XML(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg name="parentId" type="i" direction="in"/>
      <arg name="recursionDepth" type="i" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="revision" type="u" direction="out"/>
      <arg name="layout" type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="properties" type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg name="id" type="i" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg name="id" type="i" direction="in"/>
      <arg name="eventId" type="s" direction="in"/>
      <arg name="data" type="v" direction="in"/>
      <arg name="timestamp" type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg name="events" type="a(isvu)" direction="in"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg name="id" type="i" direction="in"/>
      <arg name="needUpdate" type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="updatesNeeded" type="ai" direction="out"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg name="updatedProps" type="a(ia{sv})"/>
      <arg name="removedProps" type="a(ias)"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg name="revision" type="u"/>
      <arg name="parent" type="i"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg name="id" type="i"/>
      <arg name="timestamp" type="u"/>
    </signal>
  </interface>
</node>
)XML";

struct StatusUpdatePayload {
    SniTray* tray;
    std::string text;
};

} // namespace

SniTray::SniTray() = default;

SniTray::~SniTray() {
    if (connection_) {
        if (menuRegistrationId_) {
            g_dbus_connection_unregister_object(connection_, menuRegistrationId_);
        }
        if (sniRegistrationId_) {
            g_dbus_connection_unregister_object(connection_, sniRegistrationId_);
        }
        g_object_unref(connection_);
    }
    if (menuIntrospection_) {
        g_dbus_node_info_unref(menuIntrospection_);
    }
    if (sniIntrospection_) {
        g_dbus_node_info_unref(sniIntrospection_);
    }
}

bool SniTray::Create(const std::string& iconName) {
    iconName_ = iconName;

    GError* error = nullptr;
    connection_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!connection_) {
        core::Log::Write(
            std::string("[error] Could not connect to the D-Bus session bus: ")
            + (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return false;
    }

    WarnIfNoTrayHost();

    sniIntrospection_ = g_dbus_node_info_new_for_xml(kSniIntrospectionXml, &error);
    if (!sniIntrospection_) {
        core::Log::Write(
            std::string("[error] Failed to parse StatusNotifierItem introspection XML: ")
            + (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return false;
    }

    menuIntrospection_ = g_dbus_node_info_new_for_xml(kMenuIntrospectionXml, &error);
    if (!menuIntrospection_) {
        core::Log::Write(
            std::string("[error] Failed to parse dbusmenu introspection XML: ")
            + (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return false;
    }

    static const GDBusInterfaceVTable sniVtable = {SniMethodCallThunk, SniGetPropertyThunk, nullptr};
    static const GDBusInterfaceVTable menuVtable = {MenuMethodCallThunk, MenuGetPropertyThunk, nullptr};

    sniRegistrationId_ = g_dbus_connection_register_object(
        connection_, kSniObjectPath, sniIntrospection_->interfaces[0], &sniVtable, this, nullptr, &error);
    if (!sniRegistrationId_) {
        core::Log::Write(
            std::string("[error] Failed to register the StatusNotifierItem D-Bus object: ")
            + (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return false;
    }

    menuRegistrationId_ = g_dbus_connection_register_object(
        connection_, kMenuObjectPath, menuIntrospection_->interfaces[0], &menuVtable, this, nullptr, &error);
    if (!menuRegistrationId_) {
        core::Log::Write(
            std::string("[error] Failed to register the dbusmenu D-Bus object: ")
            + (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return false;
    }

    // Build once up front so a host that calls GetLayout before ever
    // sending AboutToShow (some do, to pre-render) still sees a real menu.
    RefreshMediaSourcesAndRebuild();

    RegisterWithWatcher();

    return true;
}

void SniTray::SetInitialState(const core::AppConfig& config, bool startAtLogin) {
    config_ = config;
    startAtLogin_ = startAtLogin;
}

void SniTray::PostStatusUpdate(const std::string& text) {
    // Ownership transfers to whichever main-loop iteration handles the
    // idle callback - freed there, mirroring the Windows tray's
    // heap-allocated PostMessage payload.
    auto* payload = new StatusUpdatePayload{this, text};
    g_idle_add(ApplyStatusUpdateThunk, payload);
}

int SniTray::RunMessageLoop() {
    loop_ = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop_);
    g_main_loop_unref(loop_);
    loop_ = nullptr;
    return 0;
}

gboolean SniTray::ApplyStatusUpdateThunk(gpointer data) {
    auto* payload = static_cast<StatusUpdatePayload*>(data);
    payload->tray->ApplyStatusUpdateNow(payload->text);
    delete payload;
    return G_SOURCE_REMOVE;
}

void SniTray::ApplyStatusUpdateNow(const std::string& text) {
    // StatusNotifierItem's "ToolTip" is the closest cross-desktop analog
    // to the Windows tray tooltip - how (or whether) each desktop surfaces
    // it is shell-dependent and unverified without a real desktop session.
    // Both signals below exist for this: NewToolTip is the spec-defined
    // hook hosts are supposed to listen for; the generic
    // Properties.PropertiesChanged is emitted too as cheap extra insurance
    // for any host that only follows the standard property-invalidation
    // convention instead.
    statusText_ = text;

    if (!connection_) {
        return;
    }

    GError* error = nullptr;
    g_dbus_connection_emit_signal(
        connection_, nullptr, kSniObjectPath, kSniInterfaceName, "NewToolTip", nullptr, &error);
    if (error) {
        g_clear_error(&error);
    }

    EmitPropertyInvalidated(kSniInterfaceName, "ToolTip");
}

void SniTray::HandleCommand(int itemId) {
    auto it = menuIndex_.find(itemId);
    if (it == menuIndex_.end()) {
        return;
    }
    MenuNode* node = it->second;
    const std::string& name = node->command;

    if (name.empty()) {
        return;
    }

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

    if (name == "broadcast") {
        config_.broadcastEnabled = !config_.broadcastEnabled;
        NotifyConfigChanged();
        return;
    }

    if (name == "track-number") {
        config_.showTrackNumber = !config_.showTrackNumber;
        NotifyConfigChanged();
        return;
    }

    if (name == "start-at-login") {
        startAtLogin_ = !startAtLogin_;
        if (OnStartAtLoginChanged) {
            OnStartAtLoginChanged(startAtLogin_);
        }
        // startAtLogin_ isn't part of config_, so NotifyConfigChanged
        // (which also fires OnConfigChanged) doesn't apply - but the
        // checkbox still needs to visibly flip, so rebuild + signal
        // directly, same as NotifyConfigChanged does for everything else.
        RebuildMenuTree();
        EmitLayoutUpdated();
        return;
    }

    if (name == "tray-enabled") {
        // Cannot apply live - a running process can't remove its own tray
        // icon mid-session - main_linux.cpp's OnConfigChanged logs the
        // deferred effect, this class just flips the flag like any other
        // toggle.
        config_.trayEnabled = !config_.trayEnabled;
        NotifyConfigChanged();
        return;
    }

    if (name == "art-mode") {
        config_.artMode = node->stringValue;
        if (config_.artMode == "Custom") {
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

    if (name == "media-source") {
        config_.mediaSource = node->stringValue;
        NotifyConfigChanged();
        return;
    }

    if (name == "poll-interval") {
        config_.pollIntervalMs = node->intValue;
        NotifyConfigChanged();
        return;
    }
}

void SniTray::NotifyConfigChanged() {
    if (OnConfigChanged) {
        OnConfigChanged(config_);
    }
    RebuildMenuTree();
    EmitLayoutUpdated();
}

void SniTray::RefreshMediaSourcesAndRebuild() {
    if (OnRefreshMediaSources) {
        mediaSources_ = OnRefreshMediaSources();
    }
    RebuildMenuTree();
}

void SniTray::RebuildMenuTree() {
    menuIndex_.clear();
    nextMenuId_ = 1;
    ++menuRevision_;

    rootMenu_ = MenuNode{};
    rootMenu_.id = 0;

    auto addChild = [&](MenuNode& parent) -> MenuNode& {
        auto node = std::make_unique<MenuNode>();
        node->id = nextMenuId_++;
        MenuNode& ref = *node;
        menuIndex_[ref.id] = &ref;
        parent.children.push_back(std::move(node));
        return ref;
    };

    MenuNode& setAppIdItem = addChild(rootMenu_);
    setAppIdItem.label = "Set Discord Application ID...";
    setAppIdItem.command = "set-app-id";

    // Media Source submenu - unlike Windows (which always has "iTunes" as
    // a fixed first entry), Linux has no built-in source: the list is
    // purely whatever MPRIS players OnRefreshMediaSources found on this
    // rebuild.
    MenuNode& sourceMenu = addChild(rootMenu_);
    sourceMenu.label = "Media Source";
    sourceMenu.isSubmenu = true;
    for (const auto& source : mediaSources_) {
        MenuNode& item = addChild(sourceMenu);
        item.label = source.displayName;
        item.command = "media-source";
        item.stringValue = source.id;
        item.toggleType = "radio";
        item.toggleState = (config_.mediaSource == source.id) ? 1 : 0;
    }

    addChild(rootMenu_).isSeparator = true;

    MenuNode& broadcastItem = addChild(rootMenu_);
    broadcastItem.label = "Broadcast now playing to Discord";
    broadcastItem.command = "broadcast";
    broadcastItem.toggleType = "checkmark";
    broadcastItem.toggleState = config_.broadcastEnabled ? 1 : 0;

    MenuNode& trackNumberItem = addChild(rootMenu_);
    trackNumberItem.label = "Show track number";
    trackNumberItem.command = "track-number";
    trackNumberItem.toggleType = "checkmark";
    trackNumberItem.toggleState = config_.showTrackNumber ? 1 : 0;

    MenuNode& artMenu = addChild(rootMenu_);
    artMenu.label = "Album Art";
    artMenu.isSubmenu = true;
    auto addArtItem = [&](const char* label, const char* mode) {
        MenuNode& item = addChild(artMenu);
        item.label = label;
        item.command = "art-mode";
        item.stringValue = mode;
        item.toggleType = "radio";
        item.toggleState = (config_.artMode == mode) ? 1 : 0;
    };
    addArtItem("Automatic (look up cover art)", "Auto");
    addArtItem("Custom image URL...", "Custom");
    addArtItem("Static logo only", "Off");

    MenuNode& pollMenu = addChild(rootMenu_);
    pollMenu.label = "Poll Interval";
    pollMenu.isSubmenu = true;
    for (int ms : pollIntervalPresetsMs_) {
        MenuNode& item = addChild(pollMenu);
        item.label = std::to_string(ms / 1000) + "s";
        item.command = "poll-interval";
        item.intValue = ms;
        item.toggleType = "radio";
        item.toggleState = (config_.pollIntervalMs == ms) ? 1 : 0;
    }

    addChild(rootMenu_).isSeparator = true;

    MenuNode& startAtLoginItem = addChild(rootMenu_);
    startAtLoginItem.label = "Start automatically when you log in";
    startAtLoginItem.command = "start-at-login";
    startAtLoginItem.toggleType = "checkmark";
    startAtLoginItem.toggleState = startAtLogin_ ? 1 : 0;

    MenuNode& trayEnabledItem = addChild(rootMenu_);
    trayEnabledItem.label = "Show tray icon (applies next launch)";
    trayEnabledItem.command = "tray-enabled";
    trayEnabledItem.toggleType = "checkmark";
    trayEnabledItem.toggleState = config_.trayEnabled ? 1 : 0;

    addChild(rootMenu_).isSeparator = true;

    MenuNode& exitItem = addChild(rootMenu_);
    exitItem.label = "Exit";
    exitItem.command = "exit";
}

SniTray::MenuNode* SniTray::FindNodeById(int id) {
    if (id == 0) {
        return &rootMenu_;
    }
    auto it = menuIndex_.find(id);
    return it == menuIndex_.end() ? nullptr : it->second;
}

GVariant* SniTray::BuildPropsVariant(const MenuNode& node) const {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    if (node.isSeparator) {
        g_variant_builder_add(&builder, "{sv}", "type", g_variant_new_string("separator"));
        return g_variant_builder_end(&builder);
    }

    if (node.id != 0) {
        g_variant_builder_add(&builder, "{sv}", "label", g_variant_new_string(node.label.c_str()));
        if (!node.enabled) {
            g_variant_builder_add(&builder, "{sv}", "enabled", g_variant_new_boolean(FALSE));
        }
        if (!node.toggleType.empty()) {
            g_variant_builder_add(&builder, "{sv}", "toggle-type", g_variant_new_string(node.toggleType.c_str()));
            g_variant_builder_add(&builder, "{sv}", "toggle-state", g_variant_new_int32(node.toggleState));
        }
        if (node.isSubmenu) {
            g_variant_builder_add(&builder, "{sv}", "children-display", g_variant_new_string("submenu"));
        }
    }

    return g_variant_builder_end(&builder);
}

GVariant* SniTray::BuildLayoutVariant(const MenuNode& node) const {
    GVariant* props = BuildPropsVariant(node);

    GVariantBuilder childrenBuilder;
    g_variant_builder_init(&childrenBuilder, G_VARIANT_TYPE("av"));
    for (const auto& child : node.children) {
        g_variant_builder_add(&childrenBuilder, "v", BuildLayoutVariant(*child));
    }
    GVariant* children = g_variant_builder_end(&childrenBuilder);

    return g_variant_new("(i@a{sv}@av)", node.id, props, children);
}

GVariant* SniTray::BuildToolTipVariant() const {
    // (icon-name, icon-pixmap, title, description) per the SNI spec - no
    // pixmap data of our own (IconName carries the icon, see the header
    // comment on Create()), so this array is always empty.
    GVariantBuilder pixmapBuilder;
    g_variant_builder_init(&pixmapBuilder, G_VARIANT_TYPE("a(iiay)"));
    GVariant* pixmaps = g_variant_builder_end(&pixmapBuilder);
    return g_variant_new("(s@a(iiay)ss)", "", pixmaps, statusText_.c_str(), "");
}

void SniTray::EmitLayoutUpdated() {
    if (!connection_) {
        return;
    }
    GError* error = nullptr;
    g_dbus_connection_emit_signal(connection_, nullptr, kMenuObjectPath, kMenuInterfaceName, "LayoutUpdated",
        g_variant_new("(ui)", static_cast<guint32>(menuRevision_), 0), &error);
    if (error) {
        g_clear_error(&error);
    }
}

void SniTray::EmitPropertyInvalidated(const char* interfaceName, const char* propertyName) {
    if (!connection_) {
        return;
    }

    GVariantBuilder changedBuilder;
    g_variant_builder_init(&changedBuilder, G_VARIANT_TYPE("a{sv}"));
    GVariant* changed = g_variant_builder_end(&changedBuilder);

    GVariantBuilder invalidatedBuilder;
    g_variant_builder_init(&invalidatedBuilder, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&invalidatedBuilder, "s", propertyName);
    GVariant* invalidated = g_variant_builder_end(&invalidatedBuilder);

    GError* error = nullptr;
    g_dbus_connection_emit_signal(connection_, nullptr, kSniObjectPath, "org.freedesktop.DBus.Properties",
        "PropertiesChanged", g_variant_new("(s@a{sv}@as)", interfaceName, changed, invalidated), &error);
    if (error) {
        g_clear_error(&error);
    }
}

void SniTray::RegisterWithWatcher() {
    if (!connection_) {
        return;
    }

    const char* uniqueName = g_dbus_connection_get_unique_name(connection_);
    if (!uniqueName) {
        return;
    }

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(connection_, "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
        g_variant_new("(s)", uniqueName), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
    if (!result) {
        // Not fatal - WarnIfNoTrayHost already told the user if there is no
        // watcher at all; this covers the rarer case of a watcher that
        // exists but rejected registration for some other reason.
        core::Log::Write(
            std::string("[warn] org.kde.StatusNotifierWatcher.RegisterStatusNotifierItem failed: ")
            + (error ? error->message : "unknown error"));
        g_clear_error(&error);
        return;
    }
    g_variant_unref(result);
}

void SniTray::WarnIfNoTrayHost() {
    // Vanilla GNOME (Fedora Workstation, stock Ubuntu, etc.) ships no
    // StatusNotifierItem host at all - nothing can make a tray icon appear
    // there regardless of backend, so the right fix is telling the user
    // why, not silently doing nothing. Unchanged from the old
    // AppIndicatorTray implementation of this same check, just reusing
    // connection_ instead of opening a second bus connection for it.
    if (!connection_) {
        return;
    }

    GError* error = nullptr;
    GVariant* result = g_dbus_connection_call_sync(connection_, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameHasOwner", g_variant_new("(s)", "org.kde.StatusNotifierWatcher"),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
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

void SniTray::SniMethodCallThunk(GDBusConnection*, const char*, const char*, const char*, const char* methodName,
    GVariant* parameters, GDBusMethodInvocation* invocation, gpointer userData) {
    static_cast<SniTray*>(userData)->HandleSniMethodCall(methodName, parameters, invocation);
}

GVariant* SniTray::SniGetPropertyThunk(GDBusConnection*, const char*, const char*, const char*,
    const char* propertyName, GError** error, gpointer userData) {
    GVariant* value = static_cast<SniTray*>(userData)->GetSniProperty(propertyName);
    if (!value && error) {
        *error = g_error_new(G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No such property '%s'", propertyName);
    }
    return value;
}

void SniTray::MenuMethodCallThunk(GDBusConnection*, const char*, const char*, const char*, const char* methodName,
    GVariant* parameters, GDBusMethodInvocation* invocation, gpointer userData) {
    static_cast<SniTray*>(userData)->HandleMenuMethodCall(methodName, parameters, invocation);
}

GVariant* SniTray::MenuGetPropertyThunk(GDBusConnection*, const char*, const char*, const char*,
    const char* propertyName, GError** error, gpointer userData) {
    GVariant* value = static_cast<SniTray*>(userData)->GetMenuProperty(propertyName);
    if (!value && error) {
        *error = g_error_new(G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No such property '%s'", propertyName);
    }
    return value;
}

void SniTray::HandleSniMethodCall(
    const std::string& /*methodName*/, GVariant* /*parameters*/, GDBusMethodInvocation* invocation) {
    // Activate/SecondaryActivate/ContextMenu/Scroll - all no-ops beyond
    // returning an empty reply. Left-click/activate behavior on a tray
    // icon is host-dependent anyway; the real interaction surface is the
    // dbusmenu (ItemIsMenu is advertised as true, and Menu points at it).
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant* SniTray::GetSniProperty(const std::string& name) {
    if (name == "Category") {
        return g_variant_new_string("ApplicationStatus");
    }
    if (name == "Id") {
        return g_variant_new_string(kAppId);
    }
    if (name == "Title") {
        return g_variant_new_string("FeatherRPC");
    }
    if (name == "Status") {
        return g_variant_new_string("Active");
    }
    if (name == "IconName") {
        // IconName over IconPixmap: install.sh already installs icon.png
        // into ~/.local/share/icons/hicolor/256x256/apps/featherrpc.png,
        // and that path was already working under the old AppIndicator
        // implementation (which took the same iconName parameter and used
        // it the same way) - reusing it keeps this change purely about the
        // D-Bus transport, not the icon delivery mechanism too. The
        // IconPixmap route (raw ARGB32 bytes, no theme install dependency
        // at all) was considered and is more self-contained, but more code
        // for no behavior change on a path that already works.
        return g_variant_new_string(iconName_.c_str());
    }
    if (name == "IconThemePath") {
        return g_variant_new_string("");
    }
    if (name == "ItemIsMenu") {
        return g_variant_new_boolean(TRUE);
    }
    if (name == "Menu") {
        return g_variant_new_object_path(kMenuObjectPath);
    }
    if (name == "ToolTip") {
        return BuildToolTipVariant();
    }
    return nullptr;
}

void SniTray::HandleMenuMethodCall(
    const std::string& methodName, GVariant* parameters, GDBusMethodInvocation* invocation) {
    if (methodName == "GetLayout") {
        gint32 parentId = 0;
        gint32 recursionDepth = 0;
        GVariant* propNames = nullptr;
        // recursionDepth and propertyNames are accepted but not honored -
        // this always returns the full subtree with every property, which
        // every host tested against (and dbusmenu hosts generally) can
        // deal with fine; it's a strict superset of what was asked for.
        g_variant_get(parameters, "(ii@as)", &parentId, &recursionDepth, &propNames);
        if (propNames) {
            g_variant_unref(propNames);
        }

        MenuNode* node = FindNodeById(parentId);
        if (!node) {
            node = &rootMenu_;
        }
        GVariant* layout = BuildLayoutVariant(*node);
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(u@(ia{sv}av))", static_cast<guint32>(menuRevision_), layout));
        return;
    }

    if (methodName == "GetGroupProperties") {
        GVariant* idsV = g_variant_get_child_value(parameters, 0);
        GVariantIter idsIter;
        g_variant_iter_init(&idsIter, idsV);

        GVariantBuilder resultBuilder;
        g_variant_builder_init(&resultBuilder, G_VARIANT_TYPE("a(ia{sv})"));
        gint32 id = 0;
        while (g_variant_iter_next(&idsIter, "i", &id)) {
            MenuNode* node = FindNodeById(id);
            if (!node) {
                continue;
            }
            g_variant_builder_add(&resultBuilder, "(i@a{sv})", id, BuildPropsVariant(*node));
        }
        g_variant_unref(idsV);

        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&resultBuilder)));
        return;
    }

    if (methodName == "GetProperty") {
        gint32 id = 0;
        const char* name = nullptr;
        g_variant_get(parameters, "(i&s)", &id, &name);

        GVariant* value = nullptr;
        MenuNode* node = FindNodeById(id);
        if (node) {
            GVariant* props = BuildPropsVariant(*node);
            value = g_variant_lookup_value(props, name, nullptr);
            g_variant_unref(props);
        }
        if (!value) {
            value = g_variant_new_string("");
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
        return;
    }

    if (methodName == "Event") {
        gint32 id = 0;
        const char* eventId = nullptr;
        GVariant* data = nullptr;
        guint32 timestamp = 0;
        g_variant_get(parameters, "(i&svu)", &id, &eventId, &data, &timestamp);
        if (eventId && std::string(eventId) == "clicked") {
            HandleCommand(id);
        }
        if (data) {
            g_variant_unref(data);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    if (methodName == "EventGroup") {
        GVariant* eventsV = g_variant_get_child_value(parameters, 0);
        GVariantIter iter;
        g_variant_iter_init(&iter, eventsV);
        GVariant* child = nullptr;
        while ((child = g_variant_iter_next_value(&iter)) != nullptr) {
            gint32 id = 0;
            const char* eventId = nullptr;
            GVariant* data = nullptr;
            guint32 timestamp = 0;
            g_variant_get(child, "(i&svu)", &id, &eventId, &data, &timestamp);
            if (eventId && std::string(eventId) == "clicked") {
                HandleCommand(id);
            }
            if (data) {
                g_variant_unref(data);
            }
            g_variant_unref(child);
        }
        g_variant_unref(eventsV);

        GVariantBuilder errBuilder;
        g_variant_builder_init(&errBuilder, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@ai)", g_variant_builder_end(&errBuilder)));
        return;
    }

    if (methodName == "AboutToShow") {
        // The dbusmenu equivalent of GtkMenu's "show" signal - the hook for
        // refreshing MPRIS media sources just before the host displays the
        // menu, replacing AppIndicatorTray's old 10-second polling timer
        // (which existed only because GMenuModel-exported menus have no
        // such signal) with an actually-just-in-time refresh.
        RefreshMediaSourcesAndRebuild();
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        return;
    }

    if (methodName == "AboutToShowGroup") {
        RefreshMediaSourcesAndRebuild();
        GVariant* idsV = g_variant_get_child_value(parameters, 0);
        GVariantBuilder errBuilder;
        g_variant_builder_init(&errBuilder, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@ai@ai)", idsV, g_variant_builder_end(&errBuilder)));
        return;
    }

    g_dbus_method_invocation_return_error(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method '%s'", methodName.c_str());
}

GVariant* SniTray::GetMenuProperty(const std::string& name) {
    if (name == "Version") {
        return g_variant_new_uint32(3);
    }
    if (name == "TextDirection") {
        return g_variant_new_string("ltr");
    }
    if (name == "Status") {
        return g_variant_new_string("normal");
    }
    if (name == "IconThemePath") {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        return g_variant_builder_end(&builder);
    }
    return nullptr;
}

} // namespace nativeui
