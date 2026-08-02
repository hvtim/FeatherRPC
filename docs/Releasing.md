# Releasing

Pushing a `vX.Y.Z` tag triggers `.github/workflows/release.yml`, which
builds Windows/Linux/macOS assets and publishes a GitHub Release
automatically - see "Cutting a release" below. The manual build commands
further down in this document are the fallback/bypass path: rebuilding one
broken asset by hand without re-running (or waiting on) the whole matrix,
not the primary way releases happen. Read them as a runbook for CI,
not a replacement for it.

## Cutting a release

1. Bump `FEATHERRPC_VERSION` in `native/CMakeLists.txt`.
2. In `CHANGELOG.md`, rename `## [Unreleased]` to `## [X.Y.Z] - <date>`,
   and add a fresh empty `## [Unreleased]` above it.
3. Commit both, merge to `main`.
4. `git tag vX.Y.Z && git push origin vX.Y.Z`.

CI fails immediately, before building anything, if `FEATHERRPC_VERSION`
and the pushed tag disagree - this is the actual enforcement point that
keeps every packaging file's version in sync (see "COPR / AUR" below for
the one piece this doesn't reach).

Don't trust a rewritten or newly-touched pipeline against the real tag on
the first try - see "Testing the pipeline before trusting it" below.

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

## Symbol archiving

`CrashHandler.cpp` (Windows: `platform/windows/CrashHandler.cpp`; Linux/
macOS: `platform/posix/CrashHandler.cpp`, shared) deliberately writes raw,
unsymbolized crash reports - module name + offset on Windows, whatever
`backtrace_symbols_fd` resolves on Linux/macOS - not function names or
file/line numbers. Symbol resolution is skipped inside the handler itself
on purpose (see that file's own comments: avoiding DbgHelp/heap-touching
work while the process may already be in a corrupted state), which means
turning a reported crash address back into something readable is a
separate, offline step a maintainer does later, against the *exact*
matching build's debug symbols. That only works if those symbols were
actually archived at release time - do this for every release, not just
when a crash report shows up needing it, since the debug info for an
already-shipped build can't be regenerated later if it wasn't kept.

Each platform's build already produces (or can produce) the matching
debug artifact:

- **Windows**: `native/CMakeLists.txt` builds the Release config with
  explicit `/Zi` (compile) and `/DEBUG` (link) flags, so a real
  `FeatherRPC.pdb` (and `featherrpc-cli.pdb`) is emitted next to the exe in
  the same build - this doesn't switch the build to the separate
  `RelWithDebInfo` config, so the shipped binary's optimization level is
  unaffected. Symbolize a captured `module+offset` address against it with
  `dbghelp.dll`'s `SymLoadModuleEx`/`SymFromAddr` (or any equivalent
  WinDbg/`cdb` workflow) - confirmed working end-to-end this way: a
  deliberately captured crash address resolved back to the exact function
  and offset it actually faulted in.
- **macOS**: the `featherrpc-native` CMake target runs `dsymutil` as a
  `POST_BUILD` step (same pattern the existing icon-generation step
  uses), producing `FeatherRPC.app.dSYM` right next to `FeatherRPC.app`.
  Symbolize with `atos` or `dwarfdump` against it.
- **Linux**: no separate debug-info step is built into CMake - archive the
  *unstripped* `FeatherRPC`/`featherrpc` binaries straight out of the
  build directory (before whatever packaging step, if any, strips them
  for the actual release asset). Symbolize the raw backtrace lines
  `backtrace_symbols_fd` already wrote with `addr2line` or `objdump -d`
  against the matching unstripped binary.

Upload each platform's debug artifact as an additional GitHub Release
asset per version, reusing this doc's naming convention, e.g.:

- `FeatherRPC-<version>-windows-x64.pdb.zip` and `-windows-arm64.pdb.zip`
  (one per architecture, each zipping that architecture's own
  `FeatherRPC.pdb`/`featherrpc-cli.pdb` pair - an arm64 crash can't be
  symbolized against the x64 pdb, so these must stay separate, not
  combined into one archive)
- `FeatherRPC-<version>-macos-universal.dSYM.zip`
- `FeatherRPC-<version>-linux-x86_64-unstripped.tar.gz`

so a maintainer can later pull the one matching a reported build's
version/commit (`kBuildString` in the diagnostic report gives both) and
symbolize a raw crash address against it, rather than guessing from an
unrelated build's symbols.

## COPR / AUR

`packaging/copr/featherrpc.spec` derives its own `Version:` from
`git describe --tags` against whatever tag Packit/COPR checks out - it
never needs a manual version bump again. A GitHub Release publish (the
`publish` job's last step) fires `.packit.yaml`'s `copr_build` job
automatically; COPR's own "Auto-rebuild" toggle on the `hvtim/featherrpc`
project stays off deliberately, since that toggles a different,
every-commit-on-`main` rebuild path this project doesn't want.

`packaging/aur/PKGBUILD`'s `pkgver` has no equivalent automation - AUR
has no Packit-style integration this project uses. After each release,
bump `pkgver` by hand, run `makepkg --printsrcinfo > .SRCINFO`, and push
to the AUR git remote. This is the one release step that stays entirely
manual; see issue #13.

## Testing the pipeline before trusting it

Push a throwaway pre-release tag first, e.g. `v0.1.2-rc1`, rather than
trusting a new or changed release workflow against the real tag on the
first attempt:

- A pre-release tag's hyphenated suffix makes the GitHub Release
  auto-mark itself as a prerelease (the `publish` job checks for a `-` in
  the version).
- The same suffix gets converted for COPR/RPM's benefit -
  `v0.1.2-rc1` becomes RPM version `0.1.2~rc1` (`~` sorts before nothing,
  correctly ordering a pre-release before its final release in RPM's
  version comparison) - confirm the COPR build actually reflects that
  string rather than failing `rpmbuild` outright on an invalid `Version:`.
- Deliberately break one sanity check once (e.g. temporarily drop a
  binary from an assembled archive) to confirm the workflow actually
  fails loud instead of silently shipping - this is the direct
  re-creation-and-fix of the exact incident issue #45 describes.
- Delete the RC tag and its GitHub prerelease once satisfied, then cut
  the real tag per "Cutting a release" above.

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
