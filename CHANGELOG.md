# Changelog

Full history of this project, including under its original name and scope,
**iTunes-RPC** (an iTunes-specific Discord Rich Presence tool for Windows),
before it was rebranded to **FeatherRPC**: a lightweight, native,
cross-platform Rich Presence bridge for any media source, not just iTunes.

Format loosely follows [Keep a Changelog](https://keepachangelog.com/).
Versions before the rebrand were not strict SemVer - FeatherRPC adopts
SemVer starting at 0.1.0.

## [Unreleased]

Windows now ships a real installer
(`FeatherRPC-<version>-windows-x64-Installer.exe`/`-arm64-Installer.exe`,
built with NSIS) alongside the existing zip - same per-user install under
`%LOCALAPPDATA%\FeatherRPC`, Start Menu shortcut, optional autostart/desktop
shortcut, and Add/Remove Programs entry, behind a normal installer wizard
instead of a batch script and PowerShell prompts. See
[installer/windows/installer.nsi](installer/windows/installer.nsi).

- Release asset filenames now all carry the version, and the Windows
  installer's own name says "Installer" so it isn't mistaken for the zip
  (matches the `{app}-{version}-{os}-{arch}-Installer.exe` pattern OBS
  Studio's releases use).
- Discord Application ID setup moved out of the README's Install section
  into its own OS-agnostic Usage section - it isn't an install step.
- macOS gains a second, additional media source: "Now Playing (any app)",
  alongside the existing Music.app (Scripting Bridge) source - selectable
  from the tray's Media Source submenu or `featherrpc-cli mediasource set
  MediaRemote`. Works around Apple's macOS 15.4+ `MediaRemote.framework`
  entitlement lockdown via the vendored
  [mediaremote-adapter](https://github.com/ungive/mediaremote-adapter)
  project; Spotify excluded, same as every other platform. Unverified on
  real Mac hardware, like the rest of the macOS port - see
  [docs/KnownIssues.md](docs/KnownIssues.md).

## [0.1.1] - 2026-07-27

Linux tray rewritten to talk to `org.kde.StatusNotifierItem`/
`com.canonical.dbusmenu` directly over D-Bus, dropping
`libayatana-appindicator-glib` entirely (it wasn't reliably packaged -
AUR-only on Arch, awkward on Fedora). Live-tested on both KDE Plasma 6 and
GNOME Shell (with the AppIndicator extension).

- Cross-platform single-instance guard - previously nothing stopped two
  instances (tray+tray, headless+headless, or tray+headless) from running
  at once on any platform.
- Album art gets a second automatic lookup source, MusicBrainz + Cover Art
  Archive, tried when iTunes finds nothing. The fallback image asset key
  is now configurable from the tray menu, not just the CLI; its default
  name changed from `logo` to `fallback`.
- Linux CLI renamed `featherrpc-cli` -> `featherrpc` (Linux only - Windows/
  macOS keep the `-cli` suffix due to a filesystem case-collision issue).
- Linux distribution groundwork: AUR `PKGBUILD`, Fedora COPR `.spec`, and
  an AppImage build script - none published yet, see
  [packaging/README.md](packaging/README.md).
- Documentation restructured into `docs/` (`CLI.md`, `AlbumArt.md`,
  `Building.md`, `KnownIssues.md`) with an index, instead of everything
  living in the README or scattered at the repo root.
- Fixed: `daemon start`/`daemon restart` silently doing nothing under a
  real Linux install (wrong install-path assumption), a systemd
  restart-loop the single-instance guard's fix surfaced, `daemon start`
  reporting false success on Windows/macOS, and two Linux tray bugs found
  live on GNOME (submenus needing a click instead of opening on hover,
  and the tray icon showing a generic placeholder under a bare AppImage
  run). Full detail on all of these in
  [docs/KnownIssues.md](docs/KnownIssues.md).

## [0.1.0] - rebrand to FeatherRPC

Fresh repository, fresh SemVer starting point. Renamed from iTunes-RPC to
FeatherRPC (product identity only - `ITunesMediaSource`, `MusicApplication.h`,
and the `mediaSource: "iTunes"` config value are unchanged, since they name
Apple's app as one of several supported sources, not this project).
Baseline is the Windows tray build plus the headless CLI/daemon mode below,
carried forward unchanged in behavior. Legacy C# source dropped entirely -
its full history lives in this file and in the archived
[hvtim/iTunes-RPC](https://github.com/hvtim/iTunes-RPC) repository. Fixed the
`std::atoi` bug noted below as part of this baseline, and added a
`third_party/` path exclusion to the DevSkim workflow.

## [Unreleased] (post-3.0.0-alpha.1, pre-rebrand)

Windows-only headless CLI/daemon mode, on top of the 3.0.0-alpha.1 native
rewrite below:
- `iTunesRPC.exe --no-tray`: runs the engine with no tray icon, waiting on two
  named Windows Events instead of a UI message loop.
- New `itunesrpc-cli.exe` control tool: `appid`, `broadcast`, `tracknumber`,
  `artmode`, `arturl`, `icon`, `pollinterval`, `mediasource`, `tray`, `status`,
  `autostart`, `daemon start|stop|restart`, `config path` subcommands. Live
  config changes are pushed to a running instance via the same
  `PresenceEngine::UpdateConfig` path the tray menu already used - no new
  reload mechanism inside the engine itself.
- Tray menu gained a "Show tray icon" toggle - the one setting that can't
  apply live (a running process can't remove its own tray icon mid-session),
  explicitly called out as such rather than silently inconsistent with every
  other live-applying setting.
- `install.ps1` gained a `-NoTray` switch.
- Fixed: `std::atoi` in the CLI's `pollinterval set` parsing had undefined
  behavior on overflow and couldn't distinguish invalid input from a literal
  "0" - replaced with `std::from_chars`.
- Fixed (before ever shipping): `ShellLinkAutoLaunch`/`LaunchAgentAutoLaunch`
  hardcoded "target = whichever process calls this," which would have
  registered autostart for the short-lived CLI tool instead of the app
  the moment the CLI tool called `autostart on`.
- Fixed (Windows-specific, found during this work): `iTunesRPC.exe` and
  `itunesrpc.exe` collided on Windows' case-insensitive filesystem - one
  silently overwrote the other during a build. Fixed by naming the CLI tool
  distinctly (`itunesrpc-cli.exe`), not just differently-cased.

## [3.0.0-alpha.1] - full native rewrite

Complete rewrite from C#/.NET 8 WinForms to native C++17, no managed runtime,
per the design goal of matching native-WinAPI-tier memory usage (~1-3MB)
instead of the .NET CLR's ~32MB baseline. Cross-platform for the first time:
- **Windows**: native `Shell_NotifyIcon` tray + `TrackPopupMenu`, Win32
  dialog for the two free-text fields (Application ID, custom art URL), dark
  mode support (DWM title bar, `SetWindowTheme`, undocumented `uxtheme.dll`
  ordinals 135/136 for a dark popup menu). COM automation for iTunes
  preserved with the exact create-and-release-per-poll pattern from 1d892d8
  (see below) - this is load-bearing, not incidental. SMTC via C++/WinRT
  instead of the old WinRT.Runtime.dll/Microsoft.Windows.SDK.NET.dll
  dependency, which is now gone entirely. Verified end-to-end against
  iTunes and SMTC playback and a Discord account.
- **Linux**: MPRIS over D-Bus (works with any MPRIS-compliant player, not a
  single hardcoded app), native tray via StatusNotifierItem
  (`libayatana-appindicator-glib`, zero GTK dependency after a mid-development
  fix - see Known Issues), graceful degradation with a clear log message on
  desktops with no tray host available at all (e.g. stock GNOME). Verified
  via WSL, including a working cross-compiled aarch64 build run for real
  under `qemu-aarch64-static`.
- **macOS**: Music.app via Scripting Bridge (Apple locked down
  `MediaRemote.framework`, the only way to read *other* apps' now-playing
  state, behind entitlement checks in macOS 15.4 - Music.app-only is a
  deliberate scope decision, not an oversight). `NSStatusItem` + `NSMenu`
  tray. **Entirely unbuilt and unrun** - no Mac hardware existed anywhere in
  the development environment at any point.
- Windows ARM64 cross-compiles clean and produces an ARM64 PE binary; never
  run on ARM64 hardware.
- Installer ported: Windows keeps the Registry Uninstall-key/Start Menu/
  Desktop/Startup-shortcut approach, now via native `IShellLinkW` instead of
  `WScript.Shell`; Linux/macOS get their own install/uninstall scripts.

## [2.0.0] - tray icon and Settings window

- Tray icon with a right-click menu (Settings, Open log, Exit); the app runs
  entirely from the tray, no console window.
- Settings window: Discord Application ID (editable any time, not just
  first-run), broadcast on/off, show-track-number toggle, album art mode
  (automatic / custom URL / static logo), poll interval, start-at-login
  checkbox wired directly to the autostart shortcut.
- SMTC media source support - works with any app reporting now-playing info
  to Windows (VLC, browsers, etc.), selectable from Settings. Spotify
  excluded (it has its own Discord integration). Required retargeting to
  net8.0-windows10.0.19041.0 for the Windows Runtime APIs.
- Fixed a crash: pasting into the Application ID field threw an STA
  threading exception because `Main` wasn't `[STAThread]`.
- Fixed: settings didn't apply live until an unrelated change forced a
  resend - cosmetic-only changes now force an immediate resend instead of
  waiting for the next track change or the 60s keepalive.
- Registers in Windows Settings > Apps > Installed apps, per-user, no admin
  required; uninstall defers deleting its own directory to a detached
  process since the uninstaller runs from inside the folder it removes.
- Custom-drawn purple soundwave icon, used as the exe/tray/Settings icon.
- Extracted the polling loop into a standalone `PresenceEngine` so it can run
  independent of the UI with config swapped live.

## [1.1.0] - renamed iTunes-Sync to iTunes-RPC

"Sync" implied file/playlist syncing, not a Discord presence tool. "RPC" is
the term this category of tool already uses. Renamed the solution, project,
namespace, assembly, install directory, and log file to match.

## [1.0.2] - album art search order

Searches album title before song title for cover art - matches iTunes' own
tagging, avoids picking up a different single/compilation's cover for tracks
also released as a single, and sidesteps unusually-formatted track titles
that can miss a song-title search. Falls back to the song-title search if
there's no album tag or the album search finds nothing.

## [1.0.1] - fix silently-swallowed activity failures

`SetActivity`/`ClearActivity` now report whether the IPC write succeeded.
Previously a failed write was marked "sent" anyway, deferring the next
retry a full 60s instead of the next 2s poll - a likely contributor to
album art intermittently disappearing on longer sessions. Added persistent
file logging (`itunes-sync.log` next to the exe), since the app runs
windowless via autorun and there was previously no way to inspect what
happened during a session.

## [1.0.0] - initial release

Polls iTunes over COM, shows track/artist/album/art as a Discord "Listening
to" activity with a live progress bar, plus an installer that sets up
autostart at login.
