# Releasing

Pushing a `vX.Y.Z` tag triggers `.github/workflows/release.yml`, which
builds Windows/Linux/macOS and publishes a GitHub Release. The commands
below are the manual fallback for rebuilding one asset by hand.

## Cutting a release

1. Bump `FEATHERRPC_VERSION` in `native/CMakeLists.txt`.
2. In `CHANGELOG.md`, rename `## [Unreleased]` to `## [X.Y.Z] - <date>`,
   and add a fresh `## [Unreleased]` above it.
3. Commit, merge to `main`.
4. `git tag vX.Y.Z && git push origin vX.Y.Z`.

CI fails if the tag and `FEATHERRPC_VERSION` don't match.

Test with a pre-release tag first - see "Testing the pipeline" below.

## Asset naming

`FeatherRPC-<version>-<os>-<arch>[-suffix].<ext>`, lowercase except
`FeatherRPC`. `os` is `windows`/`linux`/`macos`. `arch` is `x86_64` for
Linux/macOS, `x64`/`arm64` for Windows.

## Building each asset

### Windows installer

```
makensis /DAPP_DIR_X64=<x64 build> /DAPP_DIR_ARM64=<arm64 build> /DVERSION=<version> installer\windows\installer.nsi
```

Produces `FeatherRPC-<version>-windows-installer.exe`, one file for both
architectures.

### Windows zip

Zip `install.bat`/`uninstall.bat`/`install.ps1`/`uninstall.ps1` + `app/`
(`FeatherRPC.exe`, `featherrpc-cli.exe`) as
`FeatherRPC-<version>-windows-x64.zip` (or `-arm64`).

### macOS dmg

```
installer/macos/build-dmg.sh --app path/to/FeatherRPC.app --output FeatherRPC-<version>-macos-universal.dmg
```

See `installer/macos/README.md`.

### AppImage

`packaging/appimage/build-appimage.sh` - see `packaging/README.md`.
Produces `FeatherRPC-<version>-x86_64.AppImage`.

### Linux tarball

```bash
chmod +x install.sh uninstall.sh app/FeatherRPC app/featherrpc
tar czf FeatherRPC-<version>-linux-x86_64.tar.gz install.sh uninstall.sh app/
```

`app/` contains `FeatherRPC`, `featherrpc`, `icon.png`.

## Symbol archiving

Archive each platform's debug symbols per release.

- **Windows**: zip `FeatherRPC.pdb`/`featherrpc-cli.pdb` per
  architecture - `FeatherRPC-<version>-windows-x64.pdb.zip` /
  `-windows-arm64.pdb.zip`.
- **macOS**: zip the `.dSYM` `dsymutil` produces -
  `FeatherRPC-<version>-macos-universal.dSYM.zip`.
- **Linux**: archive the unstripped binaries -
  `FeatherRPC-<version>-linux-x86_64-unstripped.tar.gz`.

Upload all three as additional release assets.

## COPR / AUR

`packaging/copr/featherrpc.spec` derives its version from the git tag -
no manual bump. A published GitHub Release fires `.packit.yaml`'s COPR
build automatically. COPR's own "Auto-rebuild" toggle stays off - it
would rebuild on every commit, not just releases.

`packaging/aur/PKGBUILD`'s `pkgver` has no automation - bump it by hand,
run `makepkg --printsrcinfo > .SRCINFO`, push to the AUR remote
(issue #13).

## Testing the pipeline

Push a pre-release tag first, e.g. `v0.1.2-rc1`:

- Hyphenated tags publish as a GitHub prerelease automatically.
- `v0.1.2-rc1` becomes RPM version `0.1.2~rc1` for COPR.
- Break one sanity check on purpose once, to confirm it fails loud.
- Delete the RC tag/release once satisfied, then cut the real tag.

## Writing CHANGELOG entries

Every `[Unreleased]` entry must be:

```markdown
- [Tag] One or two lines, purely descriptive.
```

`Tag` is `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, or
`Security`. Untagged or unrecognized lines are dropped with a workflow
warning.

State what changed, not why - rationale belongs in the PR/commit.

## Release notes template

Generated automatically. Blank sections are omitted.

```markdown
**vX.Y.Z.**

## Added
## Changed
## Deprecated
## Removed
## Fixed
## Security

Full detail: [KnownIssues.md](../../blob/main/docs/KnownIssues.md), [CHANGELOG.md](../../blob/main/CHANGELOG.md).
```
