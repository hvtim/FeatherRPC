# Known Issues, Quirks, and Technical Debt

A record of this project's state as of the last commit under the
iTunes-RPC name, before rebranding to FeatherRPC. Preserved so nothing
learned during development gets silently lost in the rename. Items here
are a mix of open limitations, fixed-but-noteworthy bugs, and technical
debt - none of it is glossed over.

## Open, unresolved as of this writing

### Verification gaps (not bugs - things that were never checked)

- **macOS is entirely unverified.** No Mac hardware existed anywhere in the
  development environment at any point. The Scripting Bridge glue in
  `MusicApplication.h` was hand-declared against documented APIs and has
  never been checked against Apple's own `sdef`/`sdp` scripting dictionary
  dump - property names/codes could be wrong. The tray (`NSStatusItem`/
  `NSMenu`), the `NSAlert` text prompt, and the LaunchAgent plist have never
  been run once. `installer/macos/build-dmg.sh` (the `.dmg` packaging
  script) is the same story - written against documented `hdiutil`/
  `osascript` behavior, never run. Treat all of it as "believed correct,"
  not "verified."
- **No app icon anywhere in the repo.** `assets/icon.png` (512x512) exists,
  but nobody has run `iconutil` to build an `.iconset`/`.icns` from it, and
  `native/src/platform/macos/Info.plist.in` has no `CFBundleIconFile` key.
  The shipped `.app` shows the generic macOS document icon - in Finder, the
  Dock, and the `.dmg` window itself - until this is done on Mac hardware.
- **Linux tray rendering: confirmed on one real desktop, not the other two.**
  KDE Plasma 6 on CachyOS is now confirmed working end-to-end (tray icon
  renders, menu opens, the SNI/dbusmenu rewrite talks to the real
  `org.kde.StatusNotifierItem` host) - that live test is what surfaced the
  CLI-reload gap fixed elsewhere in this file. The other two legs of the
  original 3-configuration matrix (vanilla GNOME/Wayland with no extension,
  GNOME with the AppIndicator extension) remain unwalked; everything for
  those two is still WSL-only compile/link verification, no display server.
- **Windows ARM64 has never run on ARM64 hardware.** It cross-compiles
  cleanly and the resulting binary's PE header machine type is confirmed
  `IMAGE_FILE_MACHINE_ARM64` (0xAA64), but COM/iTunes automation and
  C++/WinRT SMTC access have never been exercised on an ARM64 device.

### Deliberate scope limits (not bugs)

- **Spotify is excluded on every platform, on purpose.** It has its own
  native Discord Rich Presence integration; this app would just be a
  redundant, worse second source.
- **macOS: Music.app only, no other-app support.** Apple locked down
  `MediaRemote.framework` (the only way any third-party tool reads *other*
  apps' now-playing state on macOS) behind entitlement checks starting in
  macOS 15.4, breaking most existing workarounds. Reading other apps on Mac
  is a separately-scoped, clearly-risk-labeled feature to revisit later, not
  a gap in this implementation.
- **Windows has no background service, and never will.** A Windows Service
  (SCM-managed, Session 0) cannot `CoCreateInstance` into
  the interactive user's `iTunes.exe` COM server or see that user's
  `GlobalSystemMediaTransportControlsSessionManager` SMTC sessions - both are
  inherently tied to the interactive desktop session, by Windows design
  since Vista's Session 0 isolation. The Windows "headless" mode is
  therefore a normal per-user process launched at login (same mechanism as
  the tray app), controlled via the CLI tool, not `sc.exe` - this is a
  platform constraint, not an implementation shortcut.
- **Linux desktops with no StatusNotifierItem host cannot show a tray icon,
  full stop.** Vanilla GNOME with no AppIndicator extension exposes *no*
  tray protocol at all, not even a legacy XEmbed fallback most SNI hosts
  don't support anyway. No implementation choice fixes this - the app
  detects the absence of a `StatusNotifierWatcher` on the session bus at
  startup and logs a clear message pointing at the fix (install the
  "AppIndicator and KStatusNotifierItem Support" extension) instead of
  silently creating an icon nobody will ever see, and keeps running/updating
  Discord regardless.
- **Flatpak/Snap Discord installs on Linux are not detected.** They sandbox
  their runtime directory and nest the IPC socket differently. Logged as a
  known limitation (a first-index connection failure is ambiguous between
  "Discord isn't running" and "Discord is sandboxed differently"), not
  silently swallowed.
- **Poll interval is presets-only in the tray UI** (1s/2s/5s/10s), not a free
  numeric field - a deliberate simplification, since nothing in this app
  needs continuous/precise interval control. The CLI's `pollinterval set
  <ms>` does accept arbitrary positive values.
- **No code signing on any platform.** Binaries and installers are
  unsigned. The old C# README documented SHA256 verification of release
  downloads as a low-cost mitigation for this - worth carrying forward as a
  practice, not just a historical note. On macOS specifically, this hits
  the `.dmg` path harder than `install.sh`: a `.dmg` downloaded from a
  browser carries a quarantine flag, so Gatekeeper blocks first launch with
  "cannot be opened because the developer cannot be verified" until the
  user right-clicks > Open (or clears the flag manually). Fixing this needs
  an Apple Developer account for signing and notarization - neither exists
  for this project.

### Flagged-but-unresolved risk (low confidence either way, never tested)

- **Linux**: the tray menu rebuilds on every "show" event, sourced from
  live-refreshed MPRIS enumeration. A parallel effort flagged a theoretical
  risk that a rebuild landing while the menu is actively open could cause a
  visible flicker - never observed, since nobody's watched it render
  outside WSL.
- **macOS**: `StatusItemTray`'s `dispatch_async`-based status-update
  callback could in theory fire after the tray object is destroyed during
  shutdown, a use-after-free. Low risk, flagged, not fixed - moot until this
  code runs anywhere for the first time.

## Fixed, but worth knowing about (bugs hit during development)

- **iTunes' "scripting interface in use" quit warning.** Caused by holding a
  persistent COM connection to iTunes. Fixed (commit `1d892d8`, briefly
  reverted, then reapplied) by creating and releasing the COM object on
  every single poll instead of caching it. This is load-bearing, not
  incidental - the native C++ rewrite preserves this exact pattern
  deliberately; "optimizing" it into a persistent connection would
  reintroduce the warning.
- **Client ID masking: back-and-forth, not a clean decision.** The
  Application ID field was originally masked like a password. A later pass
  added a clarifying tooltip instead of removing the masking (commit
  `d8ac85a`) - then that whole commit was reverted with no recorded reason
  (`37a6a5a`). Later still, the masking was removed entirely (`1d892d8`),
  that got reverted (`83f81a8`), then reapplied for good (`8a04c9c`). Final
  state: the field is plain, unmasked text - Discord Application IDs are
  public identifiers, not secrets, unlike a bot token or client secret
  (neither of which this app ever stores). Documenting the flip-flopping
  honestly rather than pretending it was a straight line.
- **Unbounded IPC frame allocation.** `DiscordIpcClient.ReadFrame` originally
  trusted a 4-byte length prefix from the named pipe with no upper bound
  before allocating. Named pipes aren't authenticated, so a malicious local
  process squatting on `discord-ipc-N` could trigger a large allocation.
  Fixed with a 1MB cap in the C# version (`d8ac85a`), that
  fix was reverted for unclear/undocumented reasons (`37a6a5a`) - but the
  native rewrite's plan explicitly re-mandated this exact safeguard
  ("preserving the existing 1MB bounds check... do not drop this safeguard
  during the port"), and it is confirmed present in
  `native/src/core/DiscordIpcClient.cpp` today, cap intact, regardless of
  the C# history's back-and-forth.
- **Album art disappearing intermittently (C#, `#1`).** Root cause:
  `SetActivity`/`ClearActivity` marked an update as "sent" even when the
  underlying pipe write failed, deferring the next retry a full 60s instead
  of the next 2s poll. Fixed in `73f1457` by propagating write
  success/failure.
- **STA threading crash on paste.** Pasting into the Application ID field
  threw because `Main` wasn't `[STAThread]` (clipboard/OLE calls require
  it). Required converting from top-level statements to an explicit `Main`.
- **Settings not applying live.** Cosmetic-only changes (track number
  display, art mode) didn't reach Discord until an unrelated change forced a
  resend, since nothing marked them as needing one. Fixed by forcing an
  immediate resend on any config change, cosmetic or not - this exact
  pattern (`_forceResend`) carried into the native rewrite's
  `PresenceEngine::UpdateConfig`.
- **Copy-Item failing "file in use" during installer upgrades.** The native
  exe holds an exclusive file lock while running, unlike .NET's more lenient
  assembly loading. `install.ps1` originally only stopped the running
  process at the very end of the script - fixed by moving the stop to
  *before* the copy step, since upgrading an already-running, autostart-
  enabled install is the common case, not an edge case.
- **~25MB of orphaned .NET files after upgrading to the native build.** The
  old build shipped `Microsoft.Windows.SDK.NET.dll` and friends alongside
  `iTunesRPC.exe/.dll`; `Copy-Item` only overwrites matching filenames, so
  they'd otherwise be left behind as dead weight forever. Fixed with a
  hardcoded one-time cleanup list in the installer.
- **Linux: wrong AppIndicator library variant.** Initially linked
  `ayatana-appindicator3-0.1` (the older, GTK3-based library), which caused
  a `Gtk-CRITICAL` assertion failure when run under emulation. Root-caused,
  not patched around: switched to the `libayatana-appindicator-glib` API
  (exports menus/actions over D-Bus via `org.gtk.Menus`/`org.gtk.Actions`,
  zero GTK dependency at all - confirmed via `ldd`, no
  `libgtk`/`libgdk`/`libpango`/`libcairo` in the process). Fedora doesn't
  package this variant, so it had to be built from source for both x86_64
  and the aarch64 cross-target - a packaging wrinkle other distros may or
  may not share.
- **Linux: log directory never created on first run.** Only appeared to
  work on Windows because a prior install had already created
  `%LOCALAPPDATA%\iTunes-RPC`. Fixed in `core/Log.cpp` with an explicit
  `create_directories` call before opening the log file.
- **Linux: `PATH_MAX` used without a reliable include.** Not POSIX-
  guaranteed to exist at all. Replaced with a fixed-size buffer in
  `DesktopAutoLaunch.cpp`.
- **Windows: case-insensitive filename collision.** `iTunesRPC.exe` and a
  planned `itunesrpc.exe` CLI tool would resolve to the *same file* on
  Windows' case-insensitive-by-default filesystem - the CLI build silently
  overwrote the main app binary the first time both targets built into the
  same output directory. Fixed by giving the CLI tool a distinct name
  (`itunesrpc-cli.exe`), not just different casing. Worth remembering for
  any future Windows binary naming decisions.
- **`AutoLaunch` target-path collision (caught before shipping).**
  `ShellLinkAutoLaunch`/`LaunchAgentAutoLaunch` both hardcoded "target = the
  calling process's own executable." Harmless for the tray app calling it
  about itself, but would have silently registered autostart for the
  short-lived CLI tool instead of the app the moment the CLI tool
  called `autostart on`. Fixed by parameterizing the target path (default
  empty = self, preserving existing call sites) before the CLI tool ever
  shipped.
- **`std::atoi` UB in CLI argument parsing.** `pollinterval set <ms>` used
  `std::atoi`, undefined behavior on overflow and unable to distinguish
  invalid input from a literal "0". Fixed via `std::from_chars`.
- **Linux: CLI couldn't reach a running tray-mode instance at all.** Found
  live on a real CachyOS/KDE Plasma 6 desktop, the first real desktop this
  Linux port was ever tested on: tray mode never wrote a pidfile and never
  listened for `SIGHUP`, since `sigwait()` (headless mode's whole event
  loop) would have blocked the main thread the GLib main loop needs for the
  tray. `featherrpc appid set <id>` reported "not running" while the tray
  was genuinely running, with no way to apply a change short of restarting
  the app. Fixed by writing the pidfile unconditionally in both modes and,
  for tray mode specifically, using `g_unix_signal_add` (`glib-unix.h`) to
  hook SIGHUP/SIGTERM/SIGINT directly into the existing GLib main loop -
  chosen over a background `sigwait()` thread plus a `g_idle_add` hop back
  to the main thread, since the tray already runs entirely on this loop and
  `g_unix_signal_add` dispatches on it natively with no cross-thread
  marshaling needed at all.
- **Linux: `featherrpc-cli` renamed to `featherrpc`.** Windows/macOS keep
  the `-cli` suffix for the reason in the entry above this one (filesystem
  case-insensitivity); Linux's filesystem is case-sensitive, so `featherrpc`
  (the CLI) and `FeatherRPC` (the tray/daemon binary) can't collide there.
  The systemd user unit was also renamed from `featherrpcd.service` to
  `featherrpc.service` for the same before/after consistency, after a
  `sudo systemctl restart featherrpc` (wrong scope *and* wrong name) against
  the old name produced a confusing "unit not found" during the same test
  session.

## Design decisions that were tried, then deliberately reversed

- **`install-info.json` version-tracking mechanism.** Designed, implemented,
  and successfully tested a JSON marker file to track installed exe names
  across versions, anticipating a possible future rename. Explicitly removed
  afterward in favor of a simpler hardcoded directory-based check, on the
  reasoning that building generic tracking infrastructure for a rename that
  was merely "under consideration" (not yet decided) was premature -
  ironically, the rename did eventually happen (this document exists because
  of it), but the simpler approach was still the right call at the time; the
  actual rename was handled as its own dedicated effort (fresh repo, full
  rename sweep) rather than leaning on speculative infrastructure built
  months earlier for a different, vaguer version of the same idea.
- **Two separate binaries for tray vs. headless mode.** Originally scoped as
  `itunesrpcd` (daemon) + `iTunesRPC` (tray) as fully separate executables.
  Reversed in favor of one binary with a `--no-tray` flag, trading "the
  headless binary never links any GUI toolkit code, even dormant" for
  "one binary, one package, one installer to maintain" - accepted
  knowingly, not by default.
- **Dark-mode edit-control focus underline.** A Windows 11 search-box-style
  blue underline on the Application ID edit control was initially "fixed" by
  switching its theme so it only showed in dark mode. Reverted after
  noticing light mode showed the same underline and it wasn't a problem -
  restored consistency across both modes instead of removing a cosmetic
  detail nobody minded.

## Process notes

- `.github/workflows/devskim.yml` and `codeql.yml` exist for automated
  static analysis. As of this writing, DevSkim reports 23 alerts, of which
  11 are entirely inside the vendored `native/third_party/nlohmann/json.hpp`
  (not our code, addressed by excluding `third_party/` from the scan path
  rather than editing a vendored dependency) and the rest are false
  positives specific to this codebase's usage (single-threaded
  startup `getenv()` calls, an already-bounds-checked `memcpy`, `strlen` on
  a string literal, `localtime_s`/`localtime_r` substring-matched as
  "localtime", and Apple's literal plist DOCTYPE URL flagged as
  "insecure") - reasoning for each is preserved in this rewrite's commit
  history rather than repeated per-alert dismissals.
