# Known Issues, Quirks, and Technical Debt

Started as a record of this project's state at the moment it rebranded
from iTunes-RPC to FeatherRPC, so nothing learned during development got
silently lost in the rename. Kept up to date since - items here are a mix
of open limitations, fixed-but-noteworthy bugs, and technical debt across
the whole project's history, not just the rebrand snapshot. None of it is
glossed over.

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
  not "verified." This now also covers `MediaRemoteSource` (the "Now
  Playing (any app)" source, see the scope-limit entry below) - the
  `FetchContent`-built adapter has never actually been built or run on a
  real Mac either.
- **Linux tray rendering: confirmed on two of three desktop configurations.**
  KDE Plasma 6 (CachyOS) and GNOME Shell 50.2 with the
  `appindicatorsupport@rgcjonas.gmail.com` extension (Fedora) are both
  confirmed working end-to-end - tray icon renders, menu opens, submenus
  work correctly. Verified via `install.sh`-equivalent installs and an
  AppImage built via `packaging/appimage/` on both real desktops; the
  GNOME live test in particular surfaced two real bugs (submenu hover/
  glitch, icon placeholder - both documented below) that never showed up
  on KDE. The AUR/COPR packages are only build-and-CLI-verified so far
  (`rpmbuild`/`dnf` in a WSL Fedora environment, no display server) - not
  yet visually confirmed on a real desktop, though they carry the exact
  same tray code as the paths that are. The one remaining desktop
  configuration, vanilla GNOME/Wayland with no AppIndicator extension at
  all, is still unwalked - though per the deliberate scope limit below,
  the expected behavior there (no tray icon, a clear log message, Discord
  updates continue regardless) is simple enough that there's little left
  to verify beyond the message itself actually appearing.
- **Windows ARM64 has never run on ARM64 hardware.** It cross-compiles
  cleanly and the resulting binary's PE header machine type is confirmed
  `IMAGE_FILE_MACHINE_ARM64` (0xAA64), but COM/iTunes automation and
  C++/WinRT SMTC access have never been exercised on an ARM64 device.
- **Linux distro packaging exists but isn't published anywhere yet.** AUR
  and Fedora COPR packaging (`packaging/aur/`, `packaging/copr/`) are both
  build-verified locally, and the AppImage build
  (`packaging/appimage/`) is fully verified live on two real desktops -
  none are actually submitted/published to their respective channels yet.
  See [packaging/README.md](../packaging/README.md) for current status.

### Deliberate scope limits (not bugs)

- **Spotify is excluded on every platform, on purpose.** It has its own
  native Discord Rich Presence integration; this app would just be a
  redundant, worse second source.
- **macOS: "Now Playing (any app)" depends on an unofficial workaround, not
  a public API.** Apple locked `MediaRemote.framework` itself (the only way
  any third-party tool reads *other* apps' now-playing state on macOS)
  behind an Apple-only entitlement starting in macOS 15.4. `MediaRemoteSource`
  works around this via the vendored
  [mediaremote-adapter](https://github.com/ungive/mediaremote-adapter)
  project, which shells out to `/usr/bin/perl` (one of the few processes
  Apple still grants the entitlement to) rather than calling the framework
  directly. Apple could close this off entirely in a future macOS release
  with no warning; a lazy one-time self-test (the adapter's own `test`
  command) logs a clear warning and stops retrying if that happens, rather
  than spamming a doomed process every poll. Music.app's own source
  (`MusicMediaSource`, Scripting Bridge) is unaffected either way - it's a
  separate, official-API implementation, not built on top of this one.
  Spotify is excluded from the "any app" source for the same reason it's
  excluded everywhere else in this app (see above).
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

- **macOS**: `StatusItemTray`'s `dispatch_async`-based status-update
  callback could in theory fire after the tray object is destroyed during
  shutdown, a use-after-free. Low risk, flagged, not fixed - moot until this
  code runs anywhere for the first time.
- **macOS**: `MediaRemoteSource` depends on `/usr/bin/perl` being present.
  Apple has signaled long-term deprecation of bundled scripting runtimes
  (Perl/Python/Ruby) in macOS; it's still shipping as of the most recent
  upstream `mediaremote-adapter` testing, but there's no fallback if a
  future macOS drops it entirely - Music.app's own source is unaffected.

## Fixed, but worth knowing about (bugs hit during development)

- **macOS: no `.icns` app icon.** `native/src/platform/macos/make-icon.sh`
  now generates one from `assets/icon.png` via `iconutil`/`sips` at build
  time (regenerated on every macOS build, not a checked-in binary), and
  `Info.plist.in` points `CFBundleIconFile` at it. Confirmed live: valid
  `ic12`-type `.icns`, correctly bundled - Finder/Dock/`.dmg` windows show
  the real icon now instead of the generic document icon. This is a
  separate mechanism from the Linux tray icon (`AppIcon.h`'s raw pixel
  data) and the macOS tray icon fix below - three different places the
  same source PNG needed to end up, fixed independently.
- **macOS: tray icon didn't render in the menu bar.** `StatusItemTray`
  set the status item's title to a placeholder emoji glyph rather than a
  real image - confirmed live that this doesn't reliably render as text
  in the menu bar at all (the status item occupied space and its dropdown
  worked, but the glyph itself never appeared). Fixed by loading
  `assets/icon.png` (bundled into `Resources/icon.png`) as a real
  `NSImage` and setting it via `statusItem.button.image` instead.
- **macOS: `MusicApplication.h` didn't compile on a real SDK.**
  `SBObject`/`SBApplicationProtocol` aren't protocols in the current
  ScriptingBridge.framework (confirmed by grepping the real headers on
  macOS 15.7) - `SBObject`/`SBApplication` are plain classes. Conforms to
  `<NSObject>` instead now, matching Clang's own suggested fix; no
  behavior change, since the code only ever used the protocol's own
  declared properties.
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
- **No guard against two instances running at once, on any platform.**
  Nothing stopped tray+tray, headless+headless, or tray+headless from all
  running simultaneously, regardless of what launched the second one.
  Reproduced live on Linux: enabling autostart while a tray instance was
  already running spawned a second, independent headless instance, which
  briefly overwrote the shared pidfile and then deleted it entirely on its
  own clean shutdown - orphaning the still-running tray with no
  discoverable identity (`featherrpc status` reported "Not running" while
  it was genuinely running). Fixed with a dedicated single-instance lock
  on all three platforms (`flock()` on POSIX, a named mutex on Windows),
  acquired first thing in `main()` before touching config/engine/tray.
  Deliberately a separate mechanism from the pidfile/named-Events
  `RequestReload`/`RequestQuit` use - holding it in tray mode can't cause
  a CLI command to send a real signal into a tray process that isn't set
  up to handle it as anything other than "terminate" (true on macOS/
  Windows tray mode, which has never listened for reload/quit signals).
- **Fixing the guard above surfaced a systemd restart-loop.** Once the
  guard refused a second launch, systemd's `Restart=on-failure` treated
  that refusal as a crash and kept respawning the unit in a tight loop for
  as long as the other instance held the lock. Fixed by having the guard
  exit with a distinct code (75) and adding
  `RestartPreventExitStatus=75` to `featherrpc.service`, so systemd can
  tell an intentional refusal apart from an actual crash. Reproduced and
  confirmed fixed live (`systemctl --user status` showed "restart counter"
  climbing before the fix, a single clean "failed" state after).
- **Linux `daemon start`/`daemon restart` silently did nothing under a real
  install.** `AppExePath()` assumed the CLI tool and the main binary live
  in the same directory - false for `install.sh`'s actual layout (the app
  goes to `~/.local/share/FeatherRPC`, the CLI to `~/.local/bin` so it's on
  `$PATH`). The forked child's `execl()` failed silently before ever
  logging anything, so `daemon start` printed a misleading "Started, but
  not confirmed running yet." with nothing actually running. Fixed by
  checking `/usr/bin` and `/usr/local/bin` (for distro packages) before
  falling back to the per-user layout. Verified live both ways: a binary
  placed at `/usr/bin/FeatherRPC` is used; with nothing there, it falls
  back correctly.
- **`daemon start` also reported success when the guard above refused to
  spawn, on Windows/macOS specifically.** Windows/macOS tray mode has
  never registered itself for `IsRunning()` - only headless mode does - so
  `daemon start` had no way to tell a refused spawn (the new guard kicking
  in) apart from one that just hadn't confirmed yet, and reported the
  latter either way. Fixed by having `SpawnDaemon()` wait briefly after
  spawning and treat an early exit as a failed spawn - a healthy headless
  instance never exits on its own within a fraction of a second. Confirmed
  live on Windows: now reports "Failed to start." instead.
- **AppImage: bundling any of Fedora 44's system libraries crashed on
  dynamic-linker init, not just the ones that seemed obviously unsafe to
  bundle.** First attempt excluded the usual suspects
  (`libudev`/`libsystemd`/`libselinux`/`libmount`/`libblkid` - system-
  integration libraries that assume they're running as part of the actual
  system) after a `gdb` backtrace showed a segfault inside `libudev`'s ELF
  constructor. That fixed `libudev` specifically, but the same crash
  immediately reappeared inside a different bundled library
  (`libcbor`, curl's FIDO2/WebAuthn support - never exercised by anything
  this app does), and again inside `libcrypto` (OpenSSL, which *is*
  actually used, so excluding it isn't an option) after excluding a much
  longer list of curl's optional-protocol dependencies. This pointed at a
  more systemic incompatibility - not a specific-library problem, but bare
  incompatibility between this Fedora build's RELR-packed relocations
  (`-z pack-relative-relocs`, a newer linker feature) and loading *any* of
  its system libraries from a relocated AppImage path. Resolved by
  excluding every bundled library (`--exclude-library='*'`) and relying on
  the target system's own copies for all of them - our actual runtime
  dependencies (glib2/dbus/curl) are near-universal on desktop Linux
  already, so this isn't the portability loss it would have been with the
  old GTK/appindicator dependency footprint. Confirmed live: headless mode
  ran successfully with literally nothing bundled, after every partial
  exclude-list attempt still crashed somewhere.
- **Linux: tray submenus needed a click instead of opening on hover, then
  visibly glitched (flicker, then collapse) even on click.** Found live on
  a real Fedora/GNOME Shell 50.2 desktop, running the
  `appindicatorsupport@rgcjonas.gmail.com` extension. Root cause was on our
  side, not the extension's: `AboutToShow` (the dbusmenu "about to display
  this item" hook) was doing a synchronous MPRIS D-Bus round-trip and full
  menu rebuild on *every* item about to be shown, including submenus with
  nothing to do with media sources (Album Art, Poll Interval), and always
  hardcoded `needUpdate = true` regardless of whether anything actually
  changed. A host that's told "needs update" on every hover re-fetches and
  rebuilds its widget tree every time - GNOME's AppIndicator extension is
  documented as fragile to exactly this (see
  `ubuntu/gnome-shell-extension-appindicator#93`, an open, years-old,
  never-fully-fixed issue with the same symptom reported against several
  other apps; Tailscale's own systray hit a related timing-sensitivity bug
  on GNOME, `tailscale/tailscale#14477`). Fixed by only refreshing media
  sources when the root menu itself opens (already fresh by the time any
  submenu could be hovered) and returning `needUpdate = false` whenever
  nothing actually changed. This is core `SniTray.cpp` behavior, so it
  applies to every Linux distribution channel (AUR, COPR, AppImage,
  `install.sh`) - none of them carry a separate copy of this code.
- **Linux: tray icon showed as a generic placeholder under an AppImage run,
  with no way to fix it via `install.sh` (there's no install step at all).**
  `IconName` (a themed icon name, looked up by the host against installed
  icon themes) only resolves when something has actually put `icon.png`
  into a theme directory - true for `install.sh`, never true for a bare
  AppImage run. Confirmed live that the AppIndicator extension doesn't fall
  back to `IconPixmap` when a non-empty `IconName` fails to resolve; it just
  shows a generic icon instead. Fixed by always sending an empty `IconName`
  and relying on `IconPixmap` (raw ARGB32 pixel data, no theme lookup
  needed) exclusively - confirmed against Tailscale's own systray, which
  shows correctly on the same host/extension and uses the identical
  approach. The pixel data is baked into
  `native/src/platform/linux/AppIcon.h` ahead of time from `assets/icon.png`
  via `tools/png_to_header.py` (run manually, not part of the normal
  build - only needs re-running if the icon itself changes) - no new
  runtime dependency (no `gdk-pixbuf`, no `libpng`).
  A first attempt at building the `IconPixmap` GVariant had a real bug of
  its own: wrapping an already-complete `ay` (byte array) GVariant inside
  a second `GVariantBuilder` expecting individual byte elements is a type
  mismatch that silently produced a 0-byte array instead of the real 16384
  bytes - confirmed via `busctl` showing the property's array length as 0,
  and the tray rendering a solid grey circle, then a broken-image "X",
  instead of the actual icon.

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
