#pragma once

#include "core/AppConfig.h"
#include "core/MediaSource.h"

#include <functional>
#include <string>
#include <vector>

typedef struct _AppIndicator AppIndicator;
typedef struct _GSimpleAction GSimpleAction;
typedef struct _GVariant GVariant;
typedef struct _GMainLoop GMainLoop;
typedef int gboolean;
typedef void* gpointer;

// StatusNotifierItem tray via libayatana-appindicator-glib - the Linux
// counterpart of TrayIcon.cpp on Windows. Deliberately the -glib variant,
// not -gtk3: it exports a GMenu/GSimpleActionGroup pair straight over
// D-Bus (org.gtk.Menus/org.gtk.Actions) with no local widget tree and no
// GTK dependency at all, keeping this in line with the project's
// low-memory design goal.
namespace nativeui {

class AppIndicatorTray {
public:
    AppIndicatorTray();
    ~AppIndicatorTray();

    AppIndicatorTray(const AppIndicatorTray&) = delete;
    AppIndicatorTray& operator=(const AppIndicatorTray&) = delete;

    // iconName must resolve via the current icon theme (e.g. "featherrpc"
    // installed into ~/.local/share/icons/hicolor/.../apps/), not a raw
    // file path - AppIndicator/StatusNotifierItem looks icons up by name.
    bool Create(const std::string& iconName);

    void SetInitialState(const core::AppConfig& config, bool startAtLogin);

    // Safe to call from any thread - marshals onto the GLib main loop via
    // g_idle_add, the Linux equivalent of the Windows tray's PostMessage
    // marshaling (see the plan's Threading section).
    void PostStatusUpdate(const std::string& text);

    // Runs the GLib main loop until Exit is chosen. Blocks the calling thread.
    int RunMessageLoop();

    std::function<void(const core::AppConfig&)> OnConfigChanged;
    std::function<void(bool)> OnStartAtLoginChanged;
    std::function<void(std::string&)> OnEditApplicationId;
    std::function<void(std::string&)> OnEditCustomArtUrl;
    std::function<std::vector<core::MediaSourceInfo>()> OnRefreshMediaSources;

private:
    static void OnActivateThunk(GSimpleAction* action, GVariant* parameter, gpointer data);
    static gboolean ApplyStatusUpdateThunk(gpointer data);
    static gboolean RefreshMenuTimerThunk(gpointer data);

    void HandleActivate(GSimpleAction* action, GVariant* parameter);
    void ApplyStatusUpdateNow(const std::string& text);
    void RebuildMenu();
    void NotifyConfigChanged();
    void WarnIfNoTrayHost();

    AppIndicator* indicator_ = nullptr;
    GMainLoop* loop_ = nullptr;

    core::AppConfig config_;
    bool startAtLogin_ = false;
    std::vector<core::MediaSourceInfo> mediaSources_;
    std::vector<int> pollIntervalPresetsMs_{1000, 2000, 5000, 10000};
};

} // namespace nativeui
