# Linux distro packaging

- **[aur/](aur/)** - AUR `PKGBUILD`. Not yet submitted - pending an AUR
  account and the next tagged release (`v0.1.1` was deleted during the
  GPL-3.0 relicense; `pkgver` needs a manual bump per release, tracked in
  issue #13).
- **[copr/](copr/)** - Fedora COPR `.spec`. The `hvtim/featherrpc` COPR
  project exists and is wired to auto-build via Packit on every published
  GitHub Release (see [docs/Releasing.md](../docs/Releasing.md)); `Version:`
  derives itself from the git tag, no manual bump needed. Not yet verified
  against a real tagged release - pending the first `v0.1.2` build.
- **[appimage/](appimage/)** - `build-appimage.sh` produces a portable
  single-file AppImage. Built and bundle-verified; real-desktop run
  confirmation pending (AppImages don't run reliably under WSL).

All three build from the same install layout: `/usr/bin/{FeatherRPC,featherrpc}`,
`/usr/share/applications/featherrpc.desktop`, `/usr/share/icons/hicolor/`,
and (except AppImage, which has no systemd of its own) a `featherrpc.service`
user unit.
