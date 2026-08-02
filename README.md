<img src="assets/icon.png" width="96" height="96" alt="FeatherRPC icon">

# FeatherRPC

Shows what you're currently listening to on your Discord profile, the
same "Listening to..." status Spotify shows, but for iTunes, Apple
Music, VLC, your browser, and more.

- Windows: iTunes, VLC, browsers, or anything else that reports
  now-playing info to the system.
- Linux: most media players (anything using MPRIS, the standard most
  Linux apps support).
- macOS: Music.app, or any other app via macOS's own Now Playing.

> Built with the help of [Claude Code](https://claude.com/claude-code)
> (Anthropic's AI coding agent).

Check [docs/KnownIssues.md](docs/KnownIssues.md) for what's verified on
each platform before relying on this.

## Usage

1. Create a Discord application at
   [discord.com/developers/applications](https://discord.com/developers/applications)
   and copy its Application ID - this is what tells Discord which name
   and icon to show for your status.
2. Install FeatherRPC (see [Install](#install) below).
3. Right-click the tray icon, paste the Application ID, and set any other
   preferences from the same menu (see [Tray menu](#tray-menu) below).

The Discord desktop app has to be running - Rich Presence connects to it
locally, not through the browser.

## Install

### Windows

#### Installer (recommended)

Download `FeatherRPC-<version>-windows-installer.exe` from the
[latest release](../../releases/latest) and run it - it detects your CPU
architecture automatically. Installs to `%LOCALAPPDATA%\FeatherRPC`.

Windows will likely show a blue "Windows protected your PC" screen: this
is expected, not a sign anything's wrong - FeatherRPC just isn't
code-signed yet. Click "More info" then "Run anyway" to continue.

#### Zip

Portable, no install wizard - for scripted setups or people who'd rather
not run an installer. Download and extract
`FeatherRPC-<version>-windows-x64.zip` (`-arm64` on ARM64 Windows) from
the [latest release](../../releases/latest), then:

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
`FeatherRPC-<version>-linux-x86_64.tar.gz` from the
[latest release](../../releases/latest), then:

```bash
./install.sh
```

### macOS

Download `FeatherRPC-<version>-macos-universal.dmg` from the
[latest release](../../releases/latest), open it, and drag `FeatherRPC.app`
to the `Applications` shortcut shown alongside it.

The app is unsigned and unnotarized - no Apple Developer account exists
for this project yet (see [docs/KnownIssues.md](docs/KnownIssues.md)).
Gatekeeper will refuse to open it on first launch ("cannot be opened
because the developer cannot be verified"): right-click the app in
Finder, choose **Open**, then confirm in the dialog that appears - this
only needs doing once.

## Tray menu

Right-click the tray icon:

- Media source
- Broadcast now playing to Discord (on/off)
- **Settings** (submenu)
  - Set Discord Application ID
  - Show track number on/off
  - Album art settings (see [docs/AlbumArt.md](docs/AlbumArt.md))
  - Poll interval
  - Start automatically when you log in
  - Show tray icon (applies on next launch)
  - Verbose Logging (see [CONTRIBUTING.md](CONTRIBUTING.md))
  - Open App Directory
- Copy Diagnostic Info - for bug reports, see [CONTRIBUTING.md](CONTRIBUTING.md)

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

- Native C++, no runtime, no bundled GUI toolkit. Idle memory use is a
  few megabytes.
- Polls the selected media source every 2 seconds (COM for iTunes on
  Windows, C++/WinRT SMTC for other Windows apps, MPRIS/D-Bus on Linux,
  Scripting Bridge for Music.app on macOS or a MediaRemote-based
  workaround for any other Mac app).
- Sends a "Listening to" Rich Presence activity with a live progress bar,
  over Discord's local IPC protocol.
- Looks up cover art automatically (Apple's iTunes Search API, then
  MusicBrainz + Cover Art Archive). See [docs/AlbumArt.md](docs/AlbumArt.md).
- Spotify is excluded - it has its own Discord integration.

## Known limitations

Full detail in [docs/KnownIssues.md](docs/KnownIssues.md). Headlines:

- The "Listening to" wording isn't officially supported for third-party
  apps and could change in a future Discord update.
- Album art falls back to a static image if no automatic match is found.
- macOS is verified on Intel; Apple Silicon (arm64) compiles but has
  never run on real Apple Silicon hardware.
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
