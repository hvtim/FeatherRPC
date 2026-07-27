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

    // No icon name parameter - the icon is sent as IconPixmap data (see
    // GetProperty in SniTray.cpp), not looked up by name via IconName,
    // since a host can't resolve a name against any icon theme when
    // there was no install step to put it there (a bare AppImage run,
    // confirmed live: the icon silently fell back to generic).
    bool Create();

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
    std::function<void(std::string&)> OnEditFallbackImageKey;
    std::function<std::vector<core::MediaSourceInfo>()> OnRefreshMediaSources;

    // Invoked on the main thread (via g_unix_signal_add, from inside
    // RunMessageLoop) when SIGHUP arrives - the tray-mode equivalent of
    // main_linux.cpp's headless reload loop, so `featherrpc appid set`
    // etc. reach a running tray instance too, not just a headless one.
    std::function<void()> OnReloadRequested;

private:
    // One dbusmenu item. Built fresh into rootMenu_ by RebuildMenuTree()
    // every time config/media-source state changes or the host is about
    // to display the menu (AboutToShow). Two real dbusmenu-client bugs
    // were found here on a live desktop, both about ids, but distinct:
    //
    // 1. IDs used to be reused across rebuilds (a local counter reset to 1
    // every call). Combined with the Media Source submenu's child count
    // changing the position (and therefore the id) of every static item
    // after it, a dbusmenu client that caches widgets by id across
    // LayoutUpdated could reuse a stale one with the wrong shape: a plain
    // checkbox item ("Broadcast now playing to Discord") rendered as a
    // submenu, and another ("Show tray icon") appeared twice. Fixed by
    // giving Media Source's children ids from nextMenuId_'s range, which
    // is never reset for the SniTray object's whole lifetime (see its own
    // comment) - no id this class hands out for a dynamic child is ever
    // reused for a different item.
    //
    // 2. Separately, every *static* item's id used to change on every
    // rebuild too (same root cause: one shared, ever-incrementing counter
    // for everything). That meant "Media Source" itself never kept a
    // stable id either, so a host's later GetLayout(parentId=<the id it
    // had cached for Media Source>) stopped resolving after the next
    // rebuild - HandleMenuMethodCall's GetLayout handler fell back to
    // rootMenu_ when the requested id wasn't found, so opening the Media
    // Source submenu showed a full duplicate of the entire root menu
    // instead of the actual MPRIS player list. Fixed by giving every
    // static item (including Media Source's own node) an id from
    // RebuildMenuTree()'s separate local counter, which - unlike
    // nextMenuId_ - IS restarted at 1 every rebuild; since the static
    // items are added in the same order every time, restarting it
    // reproduces the exact same ids every time instead of new ones.
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

    std::string statusText_ = "FeatherRPC";

    core::AppConfig config_;
    bool startAtLogin_ = false;
    std::vector<core::MediaSourceInfo> mediaSources_;
    std::vector<int> pollIntervalPresetsMs_{1000, 2000, 5000, 10000};

    MenuNode rootMenu_;
    std::map<int, MenuNode*> menuIndex_;
    // Two disjoint id ranges, kept apart specifically to fix two different
    // real dbusmenu-client bugs found on a live desktop:
    //
    // 1..999 ("static" range, RebuildMenuTree()'s own local counter, NOT
    // this member): every item except the Media Source submenu's children
    // is built by the exact same sequence of addChild() calls, in the same
    // order, every single rebuild - so restarting that counter at 1 on
    // every rebuild gives every static item (including the "Media Source"
    // submenu item itself) the SAME id every time. This matters because a
    // host that already has "Media Source" cached as id 2 from an earlier
    // GetLayout will later call GetLayout(parentId=2) again to refresh
    // just that submenu - if id 2 no longer existed in menuIndex_ (which
    // it wouldn't, if this counter were the one being reset), the earlier
    // fallback-to-root bug in HandleMenuMethodCall's GetLayout handler
    // returned the WHOLE ROOT MENU mislabeled as the Media Source
    // submenu's contents - i.e. opening Media Source showed a full
    // duplicate of the entire root menu. Fixed by keeping "Media Source"
    // (and every other static item) at a permanently stable id instead.
    //
    // 1000+ (this member, nextMenuId_): reserved for the Media Source
    // submenu's children specifically - the one part of the tree whose
    // count genuinely varies, tracking live MPRIS players. Deliberately
    // never reset (see addDynamicChild in RebuildMenuTree()) for the
    // separate reason in the MenuNode comment above: reusing small ids
    // across rebuilds let a stale cached widget of the wrong shape get
    // reattached to a reused id (a checkbox rendering as a submenu, an
    // item appearing twice). The two fixes are independent but easy to
    // conflate - this one is about the Media Source item's OWN id staying
    // put; the MenuNode one is about ids never being handed out twice.
    int nextMenuId_ = 1000;
    unsigned menuRevision_ = 0;
};

} // namespace nativeui
