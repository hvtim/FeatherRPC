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

The Discord desktop app has to be running.

## Install

Downloads: [latest release](../../releases/latest).

### Windows

Run `FeatherRPC-<version>-windows-installer.exe`. Installs to
`%LOCALAPPDATA%\FeatherRPC`.

Windows will show a "Windows protected your PC" screen - click "More
info", then "Run anyway". FeatherRPC isn't code-signed yet.

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

### macOS

Open `FeatherRPC-<version>-macos-universal.dmg` and drag FeatherRPC to
Applications.

Unsigned, so macOS blocks the first launch - right-click the app, choose
Open, confirm.

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
- No Windows service.
- No code signing on any platform.

## License

GPL-3.0. See [LICENSE](LICENSE).

[native/third_party/nlohmann/json.hpp](native/third_party/nlohmann/json.hpp)
is a bundled third-party dependency and remains under its own MIT license
(see the notice at the top of that file) - relicensing FeatherRPC's own
code doesn't relicense code this project didn't write.
