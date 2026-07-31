cmake_minimum_required(VERSION 3.20)
project(FeatherRPCNative LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(WIN32)
    enable_language(RC)

    add_executable(featherrpc-native WIN32
        src/main.cpp

        # core/ - 100% portable engine layer, no platform #ifdefs, no wx dependency
        src/core/AppConfig.cpp
        src/core/Log.cpp
        src/core/DiscordIpcClient.cpp
        src/core/PresenceEngine.cpp

        # cli/ - portable status-file writer (also used headless, see --no-tray)
        src/cli/StatusFile.cpp

        # platform/windows/ - COM iTunes monitor, C++/WinRT SMTC monitor,
        # named-pipe transport, shortcut-based autorun, tray + menu + prompt UI,
        # named-Event based signaling for the separate featherrpc CLI tool
        src/platform/windows/ConfigPaths.cpp
        src/platform/windows/StringConvert.cpp
        src/platform/windows/PipeIpcTransport.cpp
        src/platform/windows/ComHelpers.cpp
        src/platform/windows/ComInit.cpp
        src/platform/windows/ITunesMediaSource.cpp
        src/platform/windows/SmtcMediaSource.cpp
        src/platform/windows/WinHttpAlbumArtLookup.cpp
        src/platform/windows/ShellLinkAutoLaunch.cpp
        src/platform/windows/TrayIcon.cpp
        src/platform/windows/TextPrompt.cpp
        src/platform/windows/DarkMode.cpp
        src/platform/windows/DaemonSignal.cpp
        src/platform/windows/resources.rc
    )

    target_include_directories(featherrpc-native PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/third_party
    )

    target_compile_definitions(featherrpc-native PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    target_link_libraries(featherrpc-native PRIVATE
        Shell32 User32 Ole32 Dwmapi UxTheme Comctl32
        Uuid       # CLSID_ShellLink/IID_IShellLinkW/IID_IPersistFile definitions
        Winhttp    # album art lookup
        WindowsApp # C++/WinRT runtime class activation (SMTC)
        Propsys    # PropVariantToString (SmtcMediaSource's app-name resolution)
        Shlwapi    # SHLoadIndirectString (SmtcMediaSource's RelaunchDisplayNameResource resolution)
        Version    # GetFileVersionInfoW/VerQueryValueW (SmtcMediaSource's process-version-info app-name fallback)
    )

    # The manifest is embedded via resources.rc (resource ID 1, RT_MANIFEST) so
    # that it can declare the Common Controls v6 dependency + DPI awareness -
    # MSVC's default linker-generated manifest would collide with it (both
    # claim the same resource ID), so it's disabled here.
    target_link_options(featherrpc-native PRIVATE "/MANIFEST:NO")

    set_target_properties(featherrpc-native PROPERTIES OUTPUT_NAME "FeatherRPC")

    # featherrpc - the thin CLI control tool (edits config.json, signals the
    # running app via named Events). Plain console subsystem (no WIN32 flag,
    # unlike the target above) and no PresenceEngine/DiscordIpcClient - it
    # never runs the engine itself, just talks to whatever's already running.
    add_executable(featherrpc-cli
        src/platform/windows/main_windows_cli.cpp

        src/core/AppConfig.cpp

        src/cli/CliDispatch.cpp
        src/cli/StatusFile.cpp

        src/platform/windows/ConfigPaths.cpp
        src/platform/windows/StringConvert.cpp
        src/platform/windows/ComHelpers.cpp
        src/platform/windows/ComInit.cpp
        src/platform/windows/ShellLinkAutoLaunch.cpp
        src/platform/windows/SmtcMediaSource.cpp
        src/platform/windows/DaemonSignal.cpp
    )

    target_include_directories(featherrpc-cli PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/third_party
    )

    target_compile_definitions(featherrpc-cli PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    target_link_libraries(featherrpc-cli PRIVATE
        Shell32 Ole32 Uuid WindowsApp Propsys
        User32     # EnumWindows/GetWindowTextW/GetWindowThreadProcessId (SmtcMediaSource.cpp)
        Shlwapi    # SHLoadIndirectString (SmtcMediaSource's RelaunchDisplayNameResource resolution)
        Version    # GetFileVersionInfoW/VerQueryValueW (SmtcMediaSource's process-version-info app-name fallback)
    )

    # NOT "featherrpc" - Windows filesystems are case-insensitive, so that
    # would collide with FeatherRPC.exe above and one would silently
    # clobber the other on build (this actually happened once - the CLI
    # build overwrote the main app binary with itself).
    set_target_properties(featherrpc-cli PROPERTIES OUTPUT_NAME "featherrpc-cli")

elseif(APPLE)
    # Universal binary (Apple Silicon + Intel) by default - CMAKE_OSX_ARCHITECTURES
    # is read once per generator run, so this must be set before the first
    # target below is configured. Nothing in src/platform/macos is
    # architecture-specific (Scripting Bridge/Cocoa/Foundation/POSIX
    # sockets are all arch-neutral, no inline asm/intrinsics), so both
    # slices build from identical source - this has not been verified by
    # an actual build (no Mac hardware in this environment).
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)

    enable_language(OBJCXX)

    # MediaRemoteSource shells out to a bundled Perl script + helper
    # framework (see MediaRemoteSource.h for why - Apple locked the
    # MediaRemote.framework API itself behind an Apple-only entitlement in
    # macOS 15.4) instead of linking against anything here. Pinned to a
    # release tag, not `main` - upstream's own README says the CLI/JSON
    # shape may still change across minor versions.
    FetchContent_Declare(mediaremote_adapter
        GIT_REPOSITORY https://github.com/ungive/mediaremote-adapter
        GIT_TAG v0.7.6
    )
    FetchContent_MakeAvailable(mediaremote_adapter)

    add_executable(featherrpc-native MACOSX_BUNDLE
        src/platform/macos/main.mm

        # core/ - 100% portable engine layer, no platform #ifdefs, no wx dependency
        src/core/AppConfig.cpp
        src/core/Log.cpp
        src/core/DiscordIpcClient.cpp
        src/core/PresenceEngine.cpp

        # cli/ - portable status-file writer (also used headless, see --no-tray)
        src/cli/StatusFile.cpp

        # platform/macos/ - Scripting Bridge Music.app monitor, MediaRemote-
        # adapter-backed "any app" monitor, LaunchAgent autorun,
        # NSStatusItem/NSMenu tray, NSAlert text-prompt dialog
        src/platform/macos/ConfigPaths.cpp
        src/platform/macos/LaunchAgentAutoLaunch.cpp
        src/platform/macos/MusicMediaSource.mm
        src/platform/macos/MediaRemoteSource.mm
        src/platform/macos/AppleSearchAlbumArtLookup.mm
        src/platform/macos/StatusItemTray.mm
        src/platform/macos/TextPrompt.mm

        # platform/posix/ - Unix-socket IPC transport + daemon signaling,
        # both shared with Linux
        src/platform/posix/UnixSocketIpcTransport.cpp
        src/platform/posix/DaemonSignal.cpp
    )

    target_include_directories(featherrpc-native PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/third_party
    )

    # ARC manages the Cocoa/Foundation object lifetimes used throughout
    # platform/macos/*.mm (NSStatusItem, NSMenu, NSAlert, etc.) - without
    # this flag those files would need manual retain/release.
    target_compile_options(featherrpc-native PRIVATE -fobjc-arc)

    target_link_libraries(featherrpc-native PRIVATE
        "-framework Cocoa"
        "-framework Foundation"
        "-framework ScriptingBridge"
    )

    # Output filename matches the Windows/old-C# build's base name so the
    # LaunchAgent plist and installer script have one name to reason about
    # across platforms.
    set_target_properties(featherrpc-native PROPERTIES
        OUTPUT_NAME "FeatherRPC"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.2"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.2"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.hvtim.featherrpc"
    )

    # MediaRemoteSource resolves these at runtime via [[NSBundle mainBundle]
    # resourcePath] - see MediaRemoteSource.mm. The adapter's own CMake
    # project ad-hoc-signs MediaRemoteAdapter.framework as its own
    # POST_BUILD step already, so nothing extra is needed here for that.
    add_custom_command(TARGET featherrpc-native POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:featherrpc-native>/Resources/mediaremote-adapter"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/../assets/icon.png"
            "$<TARGET_BUNDLE_CONTENT_DIR:featherrpc-native>/Resources/icon.png"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${mediaremote_adapter_SOURCE_DIR}/bin/mediaremote-adapter.pl"
            "$<TARGET_BUNDLE_CONTENT_DIR:featherrpc-native>/Resources/mediaremote-adapter/mediaremote-adapter.pl"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:MediaRemoteAdapter>"
            "$<TARGET_BUNDLE_CONTENT_DIR:featherrpc-native>/Resources/mediaremote-adapter/MediaRemoteAdapter.framework"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:MediaRemoteAdapterTestClient>"
            "$<TARGET_BUNDLE_CONTENT_DIR:featherrpc-native>/Resources/mediaremote-adapter/MediaRemoteAdapterTestClient"
        # CFBundleIconFile (Info.plist.in) points at this - iconutil/sips
        # are macOS-only, so this regenerates from the source PNG on every
        # macOS build rather than shipping a pre-generated binary .icns
        # that'd only be updatable by whoever next has Mac hardware handy.
        COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/make-icon.sh"
            "${CMAKE_SOURCE_DIR}/../assets/icon.png"
            "$<TARGET_BUNDLE_CONTENT_DIR:featherrpc-native>/Resources/icon.icns"
        COMMENT "Bundling mediaremote-adapter into FeatherRPC.app"
    )

    # featherrpc-cli - the thin CLI control tool. Plain command-line tool,
    # not a .app bundle (no MACOSX_BUNDLE here, unlike the target above) -
    # it needs to run from a terminal and return exit codes normally. It
    # still needs MediaRemoteSource.mm (for `mediasource list`'s fixed
    # 2-entry lookup, same as Windows/Linux's CLI targets already
    # compiling their own SmtcMediaSource.cpp/MprisMediaSource.cpp), so
    # it's not entirely Cocoa-free like it used to be - just Foundation,
    # not the AppKit/ScriptingBridge the GUI target needs.
    add_executable(featherrpc-cli
        src/platform/macos/main_macos_cli.cpp

        src/core/AppConfig.cpp
        src/core/Log.cpp # MediaRemoteSource.mm's self-test warning needs this to link

        src/cli/CliDispatch.cpp
        src/cli/StatusFile.cpp

        src/platform/macos/ConfigPaths.cpp
        src/platform/macos/LaunchAgentAutoLaunch.cpp
        src/platform/macos/MediaRemoteSource.mm
        src/platform/posix/DaemonSignal.cpp
    )

    target_include_directories(featherrpc-cli PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/third_party
    )

    target_compile_options(featherrpc-cli PRIVATE -fobjc-arc)

    target_link_libraries(featherrpc-cli PRIVATE
        "-framework Foundation"
    )

else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED dbus-1)
    # SniTray talks to org.kde.StatusNotifierItem/com.canonical.dbusmenu
    # directly over GDBus instead of linking any appindicator library (see
    # the comment atop SniTray.h for why) - gio-2.0 is the only new
    # pkg-config dependency that adds, and it pulls in glib-2.0/gobject-2.0
    # itself. No GTK anywhere in this target.
    pkg_check_modules(GIO REQUIRED gio-2.0)
    pkg_check_modules(CURL REQUIRED libcurl)

    add_executable(featherrpc-native
        src/platform/linux/main_linux.cpp

        # core/ - 100% portable engine layer, no platform #ifdefs, no wx dependency
        src/core/AppConfig.cpp
        src/core/Log.cpp
        src/core/DiscordIpcClient.cpp
        src/core/PresenceEngine.cpp

        # cli/ - portable status-file writer (also used headless, see --no-tray)
        src/cli/StatusFile.cpp

        # platform/linux/ - MPRIS/D-Bus monitor, libcurl album art lookup,
        # XDG autostart, StatusNotifierItem/dbusmenu tray + menu over GDBus,
        # zenity/kdialog text prompt
        src/platform/linux/ConfigPaths.cpp
        src/platform/linux/MprisMediaSource.cpp
        src/platform/linux/CurlAlbumArtLookup.cpp
        src/platform/linux/DesktopAutoLaunch.cpp
        src/platform/linux/SniTray.cpp
        src/platform/linux/TextPrompt.cpp

        # platform/posix/ - Unix-socket IPC transport + daemon signaling,
        # both shared with macOS
        src/platform/posix/UnixSocketIpcTransport.cpp
        src/platform/posix/DaemonSignal.cpp
    )

    target_include_directories(featherrpc-native PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/third_party
        ${DBUS_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
    )

    target_link_libraries(featherrpc-native PRIVATE
        ${DBUS_LIBRARIES}
        ${GIO_LIBRARIES}
        ${CURL_LIBRARIES}
    )

    target_compile_options(featherrpc-native PRIVATE
        ${DBUS_CFLAGS_OTHER}
        ${GIO_CFLAGS_OTHER}
        ${CURL_CFLAGS_OTHER}
    )

    # Output filename matches Windows/macOS so the installer script and any
    # cross-platform packaging tooling have one name to reason about.
    set_target_properties(featherrpc-native PROPERTIES OUTPUT_NAME "FeatherRPC")

    # featherrpc - the thin CLI control tool. Named "featherrpc", not
    # "featherrpc-cli" like Windows/macOS - Linux filesystems are
    # case-sensitive, so this doesn't collide with the "FeatherRPC" tray/
    # daemon binary above the way it would on Windows (see docs/KnownIssues.md's
    # "case-insensitive filename collision" entry for why Windows/macOS keep
    # the "-cli" suffix and must not drop it). No gio-2.0/GTK link at all
    # (this is the concrete way the headless daemon path stays free of the
    # tray dependency stack) and no PresenceEngine/DiscordIpcClient - it
    # never runs the engine, just talks to whatever's already running.
    add_executable(featherrpc
        src/platform/linux/main_linux_cli.cpp

        src/core/AppConfig.cpp

        src/cli/CliDispatch.cpp
        src/cli/StatusFile.cpp

        src/platform/linux/ConfigPaths.cpp
        src/platform/linux/MprisMediaSource.cpp
        src/platform/linux/SystemdUserAutoLaunch.cpp
        src/platform/posix/DaemonSignal.cpp
    )

    target_include_directories(featherrpc PRIVATE
        src
        ${CMAKE_SOURCE_DIR}/third_party
        ${DBUS_INCLUDE_DIRS}
    )

    target_link_libraries(featherrpc PRIVATE
        ${DBUS_LIBRARIES}
    )

    target_compile_options(featherrpc PRIVATE
        ${DBUS_CFLAGS_OTHER}
    )
endif()
