<img src="assets/icon.png" width="96" height="96" alt="FeatherRPC icon">

# FeatherRPC

Syncs whatever's currently playing to Discord as a Rich Presence status.

- Windows: iTunes, VLC, browsers, or anything else that reports
  now-playing info to the system.
- Linux: any MPRIS-compliant player.
- macOS: Music.app.

Native C++, no runtime, no bundled GUI toolkit. Idle memory use is a few
megabytes.

> Built with the help of [Claude Code](https://claude.com/claude-code)
> (Anthropic's AI coding agent).

**v0.1.0, not production-stable.** Check [docs/KnownIssues.md](docs/KnownIssues.md)
for what's verified on each platform before relying on this.

## Install

Create a Discord application at
[discord.com/developers/applications](https://discord.com/developers/applications)
and copy the Application ID - you'll enter it from the tray menu after
installing, on any platform.

### Windows

Download and run `FeatherRPC-Setup-x64.exe` (or `-arm64` on ARM64 Windows)
from the [latest release](../../releases/latest). No admin rights needed -
it installs to `%LOCALAPPDATA%\FeatherRPC`. Right-click the tray icon
afterward to enter the Application ID.

Prefer no installer? Download the Windows zip instead, extract it, and run
`install.bat`.

### Linux

Two options:

- **AppImage** - no install step, no dependencies beyond what most
  desktops already have (`glib2`, `dbus`, `curl`).
  1. Download `FeatherRPC-*.AppImage` from the
     [latest release](../../releases/latest).
  2. `chmod +x FeatherRPC-*.AppImage`
  3. Run it: `./FeatherRPC-*.AppImage`.
- **install.sh** - installs to `~/.local/share/FeatherRPC`, sets up
  autostart, and puts the `featherrpc` CLI on `$PATH`.
  1. Download the Linux zip from the
     [latest release](../../releases/latest) and extract it.
  2. `chmod +x install.sh uninstall.sh`
  3. `./install.sh`

Either way, right-click the tray icon afterward to enter the Application
ID.

### macOS

No prebuilt binary yet - see [docs/Building.md](docs/Building.md) to
build from source.

## Tray menu

Right-click the tray icon:

- Set Discord Application ID
- Media source
- Broadcast on/off
- Show track number on/off
- Album art settings (see [docs/AlbumArt.md](docs/AlbumArt.md))
- Poll interval
- Start at login
- Show tray icon (applies on next launch)

## CLI control

Control a running instance (tray or headless) from the command line.
Windows/macOS: `featherrpc-cli`. Linux: `featherrpc`.

```
featherrpc appid set <your-discord-app-id>
featherrpc status
featherrpc autostart on
```

Run with no arguments for the full command list, or see
[docs/CLI.md](docs/CLI.md).

Run the app itself with `--no-tray` for headless mode.

## How it works

- Polls the selected media source every 2 seconds (COM for iTunes on
  Windows, C++/WinRT SMTC for other Windows apps, MPRIS/D-Bus on Linux,
  Scripting Bridge for Music.app on macOS).
- Sends a "Listening to" Rich Presence activity with a live progress bar.
- Looks up cover art automatically (Apple's iTunes Search API, then
  MusicBrainz + Cover Art Archive). See [docs/AlbumArt.md](docs/AlbumArt.md).
- Spotify is excluded - it has its own Discord integration.

## Known limitations

Full detail in [docs/KnownIssues.md](docs/KnownIssues.md). Headlines:

- The "Listening to" wording isn't officially supported for third-party
  apps and could change in a future Discord update.
- Album art falls back to a static image if no automatic match is found.
- macOS is code-complete but has never been built or run - no Mac
  hardware was available during development.
- Linux tray rendering is confirmed on KDE Plasma 6; other desktop
  environments are untested.
- Windows ARM64 compiles but has never run on real ARM64 hardware.
- No Windows *service* here - Session 0 isolation blocks access to the
  interactive user's iTunes/SMTC session.
- No code signing on any platform.
