<img src="assets/icon.png" width="96" height="96" alt="FeatherRPC icon">

# FeatherRPC

Shows what you're currently listening to on your Discord profile, the
same "Listening to..." status Spotify shows, but for iTunes, Apple
Music, VLC, your browser, and more.

> Built with the help of [Claude Code](https://claude.com/claude-code)
> (Anthropic's AI coding agent).

Check [docs/KnownIssues.md](docs/KnownIssues.md) for what's verified on
each platform before relying on this.

## Usage

1. Create a Discord application at
   [discord.com/developers/applications](https://discord.com/developers/applications)
   and copy its Application ID.
2. Install FeatherRPC (see [Install](#install) below).
3. Right-click the tray icon, paste the Application ID, and set any other
   preferences from the same menu (see [Tray menu](#tray-menu) below).

The Discord desktop app has to be running, and Rich Presence must be enabled.

## Install

Downloads: [latest release](../../releases/latest).

### Windows

Run `FeatherRPC-<version>-windows-installer.exe`. Installs to
`%LOCALAPPDATA%\FeatherRPC`.

Windows will show a "Windows protected your PC" screen - click "More
info", then "Run anyway".

Prefer a zip? Extract `FeatherRPC-<version>-windows-x64.zip` (`-arm64` on
ARM64) and run `install.bat`.

### Linux

AppImage:

```bash
chmod +x FeatherRPC-*.AppImage
./FeatherRPC-*.AppImage
```

Or extract `FeatherRPC-<version>-linux-x86_64.tar.gz` and run
`./install.sh` - installs to `~/.local/share/FeatherRPC`, sets up
autostart, adds `featherrpc` to `$PATH`.

Fedora (COPR):

```bash
sudo dnf copr enable hvtim/featherrpc
sudo dnf install featherrpc
```

### macOS

Open `FeatherRPC-<version>-macos-universal.dmg` and drag FeatherRPC to
Applications.

macOS blocks the first launch because the app is unsigned. 
To bypass Gatekeeper, go to `System Settings` > `Privacy & Security`, 
scroll to the bottom and click `Allow Anyway`.

## Tray menu

Right-click the tray icon:

- Media source
- Broadcast on/off
- Settings - Discord Application ID, track number, album art (see
  [docs/AlbumArt.md](docs/AlbumArt.md)), poll interval, start at login,
  tray icon, verbose logging
- Copy Diagnostic Info (see [CONTRIBUTING.md](CONTRIBUTING.md))

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

- Native C++, no runtime. Idle memory use is a few megabytes.
- Shows a "Listening to" status with a live progress bar.
- Looks up cover art automatically. See [docs/AlbumArt.md](docs/AlbumArt.md).
- Spotify is excluded.

## Known limitations

Full detail in [docs/KnownIssues.md](docs/KnownIssues.md). Headlines:

- The "Listening to" wording isn't officially supported for third-party
  apps and could change in a future Discord update.
- Album art falls back to a static image if no automatic match is found.
- Linux tray rendering is only confirmed on KDE Plasma 6 and Gnome.
- _AppIndicator and KStatusNotifierItem_ Extension is required on Gnome
  for the tray icon to render. You can install it via [Gnome Shell Extensions](https://extensions.gnome.org/extension/615/appindicator-support/).
- Windows ARM64 compiles but has never run on real ARM64 hardware.
- No Windows service.
- No code signing on any platform.

## License

GPL-3.0. See [LICENSE](LICENSE).

[native/third_party/nlohmann/json.hpp](native/third_party/nlohmann/json.hpp)
is a bundled third-party dependency and stays under its own MIT license.
