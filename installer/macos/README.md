# macOS install

Two release assets, both built from the same `FeatherRPC.app`:

- **`FeatherRPC-<version>-macos-universal.dmg`** - the normal path for
  most users. Drag `FeatherRPC.app` to `Applications`, done. No autostart,
  no CLI tool - just the app.
- **`FeatherRPC-<version>-macos-universal-cli.zip`** - `FeatherRPC.app` +
  `featherrpc-cli` + `install.sh`/`uninstall.sh`. Run `./install.sh` to
  install to `~/Applications` (no admin needed), register a LaunchAgent
  for autostart, and install `featherrpc-cli` to `~/.local/bin`. Use
  `uninstall.sh` to remove everything it installed.

Neither replaces the other - `.dmg` is the default; the CLI zip is for
anyone who wants `featherrpc-cli` or a scripted/no-GUI install.

`~/.local/bin` isn't on `$PATH` by default on macOS - add it yourself, e.g.
`echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc`.

## Building the .dmg (maintainer, on real Mac hardware)

1. Build the app normally:
   ```
   cd native
   cmake -B build
   cmake --build build --config Release
   ```
   This produces `build/FeatherRPC.app`. Don't pass `-G Xcode` - that
   generator needs the full Xcode.app. The default generator
   (Makefiles/Ninja) needs only the Command Line Tools.

2. Run the packaging script against that bundle:
   ```
   installer/macos/build-dmg.sh --app path/to/FeatherRPC.app --output FeatherRPC-0.1.2-macos-universal.dmg
   ```
   Produces a compressed `.dmg` with `FeatherRPC.app` and an
   `Applications` symlink in a styled Finder window. See
   `build-dmg.sh --help` for options (volume name, background image,
   `--no-finder-layout` to skip Finder styling).

   Finder styling needs a local Terminal session, not SSH - it times out
   over SSH waiting on a TCC permission prompt. `--no-finder-layout`
   still produces a working, unstyled `.dmg`.

3. Attach the resulting `.dmg` to the GitHub release alongside the Windows
   zip and Linux tarball.

`.github/workflows/release.yml` now runs steps 1-2 automatically on
every tagged release, on GitHub's own macOS runners. This section is the
manual fallback if you need to rebuild the `.dmg` outside that workflow.

The CLI zip is assembled and attached automatically by the same workflow -
nothing to build manually there.

## Known limitations

Nothing here is signed or notarized - see
[docs/KnownIssues.md](../../docs/KnownIssues.md) for what that means for
anyone downloading either asset (Gatekeeper will warn on first launch).
