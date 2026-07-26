#pragma once

#include "core/AppConfig.h"
#include "core/MediaSource.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Opaque GLib/GDBus (gio-2.0) types, forward-declared exactly the way the
// old AppIndicatorTray.h forward-declared its own GLib types - full
// definitions only needed in SniTray.cpp, which includes <gio/gio.h>.
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct _GDBusNodeInfo GDBusNodeInfo;
typedef struct _GVariant GVariant;
typedef struct _GMainLoop GMainLoop;
typedef struct _GError GError;
typedef unsigned int guint;
typedef int gboolean;
typedef void* gpointer;

namespace nativeui {

// StatusNotifierItem tray - the Linux counterpart of TrayIcon.cpp
// (Windows) and StatusItemTray.mm (macOS). Talks directly to the
// org.kde.StatusNotifierItem and com.canonical.dbusmenu D-Bus interfaces
// via GDBus instead of linking any appindicator library: GLib/GIO is
// already a hard dependency of this file's predecessor (see
// WarnIfNoTrayHost, unchanged from before) and is far more reliably
// packaged across distros than libayatana-appindicator*, which turned out
// to not be reliably packaged anywhere (AUR-only on Arch, awkward on
// Fedora). Implementing the protocol ourselves removes that dependency
// class entirely rather than trading one variant of it for another.
class SniTray {
public:
    SniTray();
    ~SniTray();

    SniTray(const SniTray&) = delete;
    SniTray& operator=(const SniTray&) = delete;

    // iconName must resolve via the current icon theme (e.g. "featherrpc"
    // installed into ~/.local/share/icons/hicolor/.../apps/ by
    // installer/linux/install.sh) - sent to the host as the
    // StatusNotifierItem "IconName" property and looked up by name, the
    // same contract AppIndicator/libappindicator used. Chosen over
    // embedding IconPixmap data because install.sh's icon-theme step
    // already works and is already verified; see SniTray.cpp for the
    // full tradeoff writeup.
    bool Create(const std::string& iconName);

    void SetInitialState(const core::AppConfig& config, bool startAtLogin);

    // Safe to call from any thread - marshals onto the GLib main loop via
    // g_idle_add, the Linux equivalent of the Windows tray's PostMessage
    // marshaling.
    void PostStatusUpdate(const std::string& text);

    // Runs the GLib main loop until Exit is chosen (or SIGTERM/SIGINT is
    // received - see RunMessageLoop()'s g_unix_signal_add hookup). Blocks
    // the calling thread.
    int RunMessageLoop();

    // Dispatches a dbusmenu item id (as delivered by the host's Event
    // call) to the matching config change/callback - the direct
    // replacement for the old GAction-name-based HandleActivate, now that
    // menu items are plain integer ids instead of named GActions.
    void HandleCommand(int itemId);

    std::function<void(const core::AppConfig&)> OnConfigChanged;
    std::function<void(bool)> OnStartAtLoginChanged;
    std::function<void(std::string&)> OnEditApplicationId;
    std::function<void(std::string&)> OnEditCustomArtUrl;
    std::function<std::vector<core::MediaSourceInfo>()> OnRefreshMediaSources;

    // Invoked on the main thread (via g_unix_signal_add, from inside
    // RunMessageLoop) when SIGHUP arrives - the tray-mode equivalent of
    // main_linux.cpp's headless reload loop, so `featherrpc appid set`
    // etc. reach a running tray instance too, not just a headless one.
    std::function<void()> OnReloadRequested;

private:
    // One dbusmenu item. Built fresh into rootMenu_ by RebuildMenuTree()
    // every time config/media-source state changes or the host is about
    // to display the menu (AboutToShow). IDs are assigned from nextMenuId_,
    // which is deliberately NEVER reset across rebuilds (see its own
    // comment) - a live desktop test found that reusing small ids across
    // rebuilds, combined with the Media Source submenu's child count
    // changing the position (and therefore the id) of every item after it,
    // caused a real dbusmenu client to visibly misrender: a plain checkbox
    // item ("Broadcast now playing to Discord") rendered as a submenu, and
    // another ("Show tray icon") appeared twice. Every id this class ever
    // hands out is unique for the lifetime of the SniTray object, not just
    // between one rebuild and the next, specifically so a host that caches
    // widgets by id across LayoutUpdated can never reuse a stale one.
    // Held by unique_ptr (not by value) inside std::vector<> so that
    // menuIndex_'s raw pointers into this tree stay valid even as siblings
    // are appended and the vector reallocates.
    struct MenuNode {
        int id = 0;
        std::string label;
        bool isSeparator = false;
        bool enabled = true;
        std::string toggleType;  // "checkmark", "radio", or "" for none
        int toggleState = -1;    // 0/1/-1 per the dbusmenu spec
        bool isSubmenu = false;
        std::string command;     // internal command name, e.g. "media-source"
        std::string stringValue; // parameter for media-source/art-mode commands
        int intValue = 0;        // parameter for the poll-interval command
        std::vector<std::unique_ptr<MenuNode>> children;
    };

    static void SniMethodCallThunk(GDBusConnection* connection, const char* sender, const char* objectPath,
        const char* interfaceName, const char* methodName, GVariant* parameters, GDBusMethodInvocation* invocation,
        gpointer userData);
    static GVariant* SniGetPropertyThunk(GDBusConnection* connection, const char* sender, const char* objectPath,
        const char* interfaceName, const char* propertyName, GError** error, gpointer userData);
    static void MenuMethodCallThunk(GDBusConnection* connection, const char* sender, const char* objectPath,
        const char* interfaceName, const char* methodName, GVariant* parameters, GDBusMethodInvocation* invocation,
        gpointer userData);
    static GVariant* MenuGetPropertyThunk(GDBusConnection* connection, const char* sender, const char* objectPath,
        const char* interfaceName, const char* propertyName, GError** error, gpointer userData);
    static gboolean ApplyStatusUpdateThunk(gpointer data);
    static gboolean OnSigHupThunk(gpointer data);
    static gboolean OnSigQuitThunk(gpointer data);

    void HandleSniMethodCall(
        const std::string& methodName, GVariant* parameters, GDBusMethodInvocation* invocation);
    GVariant* GetSniProperty(const std::string& propertyName);

    void HandleMenuMethodCall(
        const std::string& methodName, GVariant* parameters, GDBusMethodInvocation* invocation);
    GVariant* GetMenuProperty(const std::string& propertyName);

    void ApplyStatusUpdateNow(const std::string& text);
    void RegisterWithWatcher();
    void WarnIfNoTrayHost();
    void NotifyConfigChanged();

    void RefreshMediaSourcesAndRebuild();
    void RebuildMenuTree();
    MenuNode* FindNodeById(int id);

    void EmitLayoutUpdated();
    void EmitPropertyInvalidated(const char* interfaceName, const char* propertyName);

    GVariant* BuildPropsVariant(const MenuNode& node) const;
    GVariant* BuildLayoutVariant(const MenuNode& node) const;
    GVariant* BuildToolTipVariant() const;

    GDBusConnection* connection_ = nullptr;
    GDBusNodeInfo* sniIntrospection_ = nullptr;
    GDBusNodeInfo* menuIntrospection_ = nullptr;
    guint sniRegistrationId_ = 0;
    guint menuRegistrationId_ = 0;
    GMainLoop* loop_ = nullptr;

    std::string iconName_;
    std::string statusText_ = "FeatherRPC";

    core::AppConfig config_;
    bool startAtLogin_ = false;
    std::vector<core::MediaSourceInfo> mediaSources_;
    std::vector<int> pollIntervalPresetsMs_{1000, 2000, 5000, 10000};

    MenuNode rootMenu_;
    std::map<int, MenuNode*> menuIndex_;
    // Monotonically increasing for the lifetime of this object - RebuildMenuTree()
    // must NOT reset this to 1. It used to, which is what caused the
    // submenu-misrender/duplicate-item bugs described on MenuNode above:
    // the Media Source submenu's child count varies (it tracks live MPRIS
    // players), so resetting ids each rebuild meant every item after it
    // landed on a different id than before, and a dbusmenu client that
    // caches widgets by id across rebuilds could reuse a stale one with
    // the wrong shape (submenu vs. checkbox) or fail to discard the old
    // one (duplicate item).
    int nextMenuId_ = 1;
    unsigned menuRevision_ = 0;
};

} // namespace nativeui
