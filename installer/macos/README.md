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

- **`build-dmg.sh` has never been run.** It's written against documented
  `hdiutil`/`osascript` behavior and the AppleScript pattern common to other
  open source Mac apps' release scripts, not verified on real hardware.
  Same caveat as the rest of this project's macOS support - treat it as
  believed-correct, not tested.
- **No app icon.** `native/src/platform/macos/Info.plist.in` has no
  `CFBundleIconFile` key, and there's no `.icns` anywhere in the repo -
  `assets/icon.png` (512x512) exists and is a fine source, but nobody has
  run `iconutil` to build the `.iconset`/`.icns` from it or wired the result
  into the bundle. Until that happens, the shipped `.app` shows the generic
  macOS document icon everywhere (Finder, Dock, the `.dmg` window itself).
- **Unsigned and not notarized.** No Apple Developer account has ever been
  used in this project. A `.dmg` downloaded from the internet and opened
  will hit Gatekeeper's "cannot be opened because the developer cannot be
  verified" block on first launch - the user has to right-click > Open (or
  clear the quarantine flag) to get past it. `install.sh` run locally from
  an already-extracted folder doesn't trigger the same download-quarantine
  flag the same way, which is part of why the `.dmg` needs this called out
  specifically rather than assumed equivalent to the script path. Real fix
  is code signing + notarization, both of which need an Apple Developer
  account and real hardware - out of scope here.
