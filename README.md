<img src="assets/icon.png" width="96" height="96" alt="FeatherRPC icon">

# FeatherRPC

Syncs whatever's currently playing to Discord as a Rich Presence status.
iTunes, VLC, browsers, or anything else that reports now-playing info to
Windows; any MPRIS player on Linux; Music.app on macOS. Native C++, no
runtime, no bundled GUI toolkit. Idle memory use is a few megabytes.

> Built with the help of [Claude Code](https://claude.com/claude-code)
> (Anthropic's AI coding agent).

Used to be called iTunes-RPC and only worked with iTunes on Windows.
Renamed and rewritten to match what it actually does now. The old project's
history, including its own `CHANGELOG.md` and `KNOWN_ISSUES.md`, is kept at
the archived [hvtim/iTunes-RPC](https://github.com/hvtim/iTunes-RPC) repo.

**v0.1.0, not production-stable.** Check [KNOWN_ISSUES.md](KNOWN_ISSUES.md)
for what's actually verified on each platform before relying on this.

## Install

### Windows

1. Create a Discord application at
   [discord.com/developers/applications](https://discord.com/developers/applications)
   and copy the Application ID.
2. Download the Windows zip from the
   [latest release](../../releases/latest) and extract it.
3. Run `install.bat`. Right-click the tray icon afterward to enter the
   Application ID.

Installs to `%LOCALAPPDATA%\FeatherRPC`. Pass `-NoTray` to `install.ps1`
(or edit the shortcut afterward) to run headless instead - see
[Headless / CLI mode](#headless--cli-mode-windows) below.

### Linux / macOS

See [installer/linux](installer/linux) / [installer/macos](installer/macos).
macOS has no prebuilt binary yet - see [Build from source](#build-from-source).

## Tray menu

Right-click the tray icon:

- Set Discord Application ID
- Media source (Windows: iTunes or any SMTC-reporting app; Linux: any
  MPRIS-compliant player; macOS: Music.app only for now)
- Broadcast on/off
- Show track number on/off
- Album art: automatic lookup, a custom image URL, or a static logo only
- Poll interval
- Start at login
- Show tray icon (Windows; toggling this off takes effect on the next
  launch, not live - a running process can't remove its own tray icon
  mid-session)

Every change applies immediately to the running instance except the tray
toggle itself.

## Headless / CLI mode (Windows)

`FeatherRPC.exe --no-tray` runs with no tray icon at all. Control it with the
bundled `featherrpc-cli.exe`:

```
featherrpc-cli appid set <your-discord-app-id>
featherrpc-cli status
featherrpc-cli pollinterval set 5000
featherrpc-cli autostart on
featherrpc-cli daemon stop
```

Run `featherrpc-cli` with no arguments for the full command list. Linux/
macOS CLI parity is in progress (see open branches).

## Build from source

Requires CMake 3.20+ and a C++17 compiler.

```
cd native
cmake -B build -G "Visual Studio 17 2022" -A x64   # or your platform's generator
cmake --build build --config Release
```

Windows also cross-compiles for ARM64 with `-A ARM64` (a v143 ARM64 build
tools component is required); this has never been run on real ARM64
hardware.

macOS: `cmake -B build -G Xcode && cmake --build build --config Release`
produces a `FeatherRPC.app` bundle. Requires Xcode command line tools. This
path is untested - open an issue if you hit build errors.

## How it works

- Polls the selected media source every 2 seconds (COM for iTunes on
  Windows, C++/WinRT SMTC for other Windows apps, MPRIS/D-Bus on Linux,
  Scripting Bridge for Music.app on macOS).
- Sends the track to Discord as a "Listening to" Rich Presence activity with
  a live progress bar.
- Looks up cover art via Apple's iTunes Search API (album, then track),
  regardless of platform or media source.
- Spotify is excluded on every platform - it has its own Discord
  integration.

## Known limitations

Full detail in [KNOWN_ISSUES.md](KNOWN_ISSUES.md). Headlines:

- The "Listening to" wording isn't officially supported for third-party
  apps and could change in a future Discord update.
- Album art requires a match on Apple's catalog; unmatched tracks fall back
  to a static logo.
- macOS is code-complete but has never been built or run - no Mac hardware
  was available during development.
- Linux tray rendering has never been visually confirmed on a real desktop.
- No real Windows *service* here - Session 0 isolation blocks access to the
  interactive user's iTunes/SMTC session. Headless mode is a login-launched
  process, not `sc.exe`.
