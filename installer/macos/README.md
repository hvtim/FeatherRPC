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
   cmake -B build
   cmake --build build --config Release
   ```
   This produces `build/FeatherRPC.app`. Don't pass `-G Xcode` - that
   generator needs the full Xcode.app installed, and fails outright with
   only the Xcode Command Line Tools (confirmed live - CLT alone is
   enough to build FeatherRPC, just not with this particular generator).
   The default generator (Makefiles/Ninja) works fine and needs only CLT.

2. Run the packaging script against that bundle:
   ```
   installer/macos/build-dmg.sh --app path/to/FeatherRPC.app --output FeatherRPC-0.1.2-macos.dmg
   ```
   This produces a compressed `.dmg` with `FeatherRPC.app` and an
   `Applications` symlink, both visible in a styled Finder window when
   mounted. See `build-dmg.sh --help` for all options (custom volume name,
   background image, `--no-finder-layout` to skip the Finder-scripting step
   if it fails on a headless runner).

   The Finder-styling step needs a real interactive GUI session to answer
   a macOS Automation/TCC permission prompt the first time - confirmed
   live that it times out (`AppleEvent timed out (-1712)`) when triggered
   over SSH instead, even with a logged-in GUI session, since there's
   nobody to answer the prompt. The script's own fallback still produces
   a working, just unstyled, `.dmg` in that case. Running from a real
   local Terminal (not SSH) should let the prompt be answered once.

3. Attach the resulting `.dmg` to the GitHub release alongside the Windows
   zip and Linux tarball.

There's no CI workflow doing this automatically - there's no Mac runner
available to test one against. Steps 1-2 have now been run for real
(via a Mac VM, not CI) - see [docs/KnownIssues.md](../../docs/KnownIssues.md)
for what that testing did and didn't cover.

## Known limitations

Nothing here is signed or notarized - see
[docs/KnownIssues.md](../../docs/KnownIssues.md) for what that means for
anyone downloading the `.dmg` (Gatekeeper will warn on first launch).
