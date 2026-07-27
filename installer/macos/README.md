# macOS install

Two install paths, both built from the same `FeatherRPC.app`:

- **`.dmg`** - the normal path for most users. Drag `FeatherRPC.app` to
  `Applications`, done. No autostart, no CLI tool - just the app.
- **`install.sh`** - for scripted/no-GUI setups. Installs to
  `~/Applications` (no admin needed), registers a LaunchAgent for autostart,
  and installs `featherrpc-cli` to `~/.local/bin`. Use `uninstall.sh` to
  remove everything it installed.

Neither replaces the other - the `.dmg` is the nicer default; `install.sh`
stays as the scriptable fallback for people who want autostart and the CLI
tool wired up in one step, or who are provisioning a machine without
clicking through Finder.

## Building the .dmg (maintainer, on real Mac hardware)

1. Build the app normally:
   ```
   cd native
   cmake -B build -G Xcode
   cmake --build build --config Release
   ```
   This produces `build/Release/FeatherRPC.app` (path depends on the CMake
   generator - Xcode's multi-config layout puts it under
   `build/<Scheme>/Release/`, check where your build actually landed it).

2. Run the packaging script against that bundle:
   ```
   installer/macos/build-dmg.sh --app path/to/FeatherRPC.app --output FeatherRPC-0.1.0.dmg
   ```
   This produces a compressed `.dmg` with `FeatherRPC.app` and an
   `Applications` symlink, both visible in a styled Finder window when
   mounted. See `build-dmg.sh --help` for all options (custom volume name,
   background image, `--no-finder-layout` to skip the Finder-scripting step
   if it fails on a headless runner).

3. Attach the resulting `.dmg` to the GitHub release alongside the Windows
   zip and Linux tarball.

There's no CI workflow doing this automatically - there's no Mac runner
available to test one against. This is a plausible flow for whenever real
Mac hardware or a hosted macOS runner is available; a maintainer with
either can run steps 1-2 above by hand for now.

## Known limitations

`build-dmg.sh` has never been run, there's no app icon, and nothing here
is signed or notarized. Full detail in
[docs/KnownIssues.md](../../docs/KnownIssues.md).
