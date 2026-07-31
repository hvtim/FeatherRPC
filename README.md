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

### Windows

#### Installer

No admin rights needed. Download
`FeatherRPC-<version>-windows-x64-Installer.exe` (`-arm64` on ARM64
Windows) from the [latest release](../../releases/latest) and run it.
Installs to `%LOCALAPPDATA%\FeatherRPC`.

#### Zip

Download and extract `FeatherRPC-<version>-windows-x64.zip` (`-arm64` on
ARM64 Windows) from the [latest release](../../releases/latest), then:

```
install.bat
```

### Linux

#### AppImage

No install step, no dependencies beyond what most desktops already have
(`glib2`, `dbus`, `curl`). Download from the
[latest release](../../releases/latest), then:

```bash
chmod +x FeatherRPC-*.AppImage
./FeatherRPC-*.AppImage
```

#### Install script

Installs to `~/.local/share/FeatherRPC`, sets up autostart, and puts the
`featherrpc` CLI on `$PATH`. Download and extract
`FeatherRPC-<version>-linux-x64.zip` from the
[latest release](../../releases/latest), then:

```bash
chmod +x install.sh uninstall.sh
./install.sh
```

### macOS

No prebuilt binary yet - see [docs/Building.md](docs/Building.md) to
build from source.

## Usage

1. Create a Discord application at
   [discord.com/developers/applications](https://discord.com/developers/applications)
   and copy its Application ID. No bot account or OAuth setup needed -
   Rich Presence connects over Discord's local IPC protocol, not the bot
   API.
2. Install FeatherRPC (see [Install](#install) above).
3. Right-click the tray icon, paste the Application ID, and set any other
   preferences from the same menu (see [Tray menu](#tray-menu) below).

The Discord desktop app has to be running - Rich Presence connects to it
locally, not through the browser.

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

## License

GPL-3.0. See [LICENSE](LICENSE).

[native/third_party/nlohmann/json.hpp](native/third_party/nlohmann/json.hpp)
is a bundled third-party dependency and remains under its own MIT license
(see the notice at the top of that file) - relicensing FeatherRPC's own
code doesn't relicense code this project didn't write.
