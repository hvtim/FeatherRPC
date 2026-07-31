# Releasing

There's no checked-in release-packaging automation yet (no
`.github/workflows/release*.yml`, no packaging script beyond what's under
`installer/`) - releases are cut by hand. This is the checklist to follow:
asset naming, how each asset gets built today, and the release notes
template.

## Asset naming

`FeatherRPC-<version>-<os>-<arch>[-suffix].<ext>`, lowercase except the
literal `FeatherRPC` token. `os` is `windows`/`linux`/`macos` (never
`osx`). `arch` is `x86_64` (never `x64`) for Linux and macOS. Windows
keeps its own ecosystem's native arch terms instead - `x64` and `arm64`,
not `x86_64`/`aarch64` - since that's what Windows itself calls these
architectures.

| Asset | Before | After |
|---|---|---|
| Windows installer | `-windows-x64-Installer.exe` / `-arm64-Installer.exe` (two files) | `-windows-installer.exe` (one combined file, auto-detects CPU at install time - #30) |
| Windows zip | `-windows-x64.zip` / `-arm64.zip` | unchanged, already compliant |
| Linux archive | `-linux-x64.zip` | `-linux-x86_64.tar.gz` (format change - see below) |
| macOS DMG | `-macos-universal.dmg` | unchanged, already compliant |
| AppImage | `-x86_64.AppImage` | unchanged, already compliant |

## Building each asset

### Windows installer

`makensis /DAPP_DIR_X64=<path to x64 build> /DAPP_DIR_ARM64=<path to
arm64 build> /DVERSION=<version> installer\windows\installer.nsi` - see
the header comment in `installer/windows/installer.nsi`. Produces a
single `FeatherRPC-<version>-windows-installer.exe` that embeds both
architectures' binaries and picks the matching pair at install time via
native CPU detection (`x64.nsh`) - no more separate `x64`/`arm64`
installer downloads.

### Windows zip

Zip the `install.bat`/`uninstall.bat`/`install.ps1`/`uninstall.ps1` +
`app/` (`FeatherRPC.exe`, `featherrpc-cli.exe`) layout `install.ps1`
already expects (see `installer/windows/install.ps1`). Name stays
`FeatherRPC-<version>-windows-x64.zip` / `-windows-arm64.zip` - `x64` is
Windows's own term, not `x86_64`.

### macOS DMG

`installer/macos/build-dmg.sh --app path/to/FeatherRPC.app --output
FeatherRPC-<version>-macos-universal.dmg` - see
`installer/macos/README.md` for the full build (build the `.app` first,
then run this script against it).

### AppImage

`packaging/appimage/build-appimage.sh` - see `packaging/README.md`. Name
stays `FeatherRPC-<version>-x86_64.AppImage`, already compliant.

### Linux archive

See below - format is changing from `.zip` to `.tar.gz`.

## Linux: zip to tar.gz

`.tar.gz` preserves Unix executable bits and symlinks natively; `.zip`
doesn't reliably. That's exactly why the current Linux install
instructions need a manual `chmod +x install.sh uninstall.sh` step after
extracting the zip (see `README.md`'s Linux install section) - the
executable bit doesn't survive the zip round-trip. A `.tar.gz` built with
the bit already set removes that step entirely. This replaces the zip; it
doesn't add a third option, and the contents are the same either way.

The release layout matches what `installer/linux/install.sh` already
expects next to itself: `install.sh`, `uninstall.sh`, and an `app/`
directory containing `FeatherRPC`, `featherrpc`, and `icon.png`.

```bash
chmod +x install.sh uninstall.sh app/FeatherRPC app/featherrpc
tar czf FeatherRPC-<version>-linux-x86_64.tar.gz install.sh uninstall.sh app/
```

## Release notes template

Reuses `CHANGELOG.md`'s own section names - it already commits to
"loosely follows [Keep a Changelog](https://keepachangelog.com/)". Blank
sections are omitted entirely, not shown-but-empty.

```markdown
**vX.Y.Z.** One-sentence summary.

## Added
## Changed
## Deprecated
## Removed
## Fixed
## Security

Full detail: [KnownIssues.md](../../blob/main/docs/KnownIssues.md), [CHANGELOG.md](../../blob/main/CHANGELOG.md).
```
