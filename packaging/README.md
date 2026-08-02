# Linux distro packaging

- **[aur/](aur/)** - AUR `PKGBUILD`. Not yet submitted - pending an AUR
  account and a tagged release (issue #13).
- **[copr/](copr/)** - Fedora COPR `.spec`. Project exists, auto-builds
  via Packit on release (see [docs/Releasing.md](../docs/Releasing.md)).
  Not yet verified against a real release.
- **[appimage/](appimage/)** - `build-appimage.sh` produces a portable
  single-file AppImage. Built and bundle-verified; real-desktop run
  confirmation pending.

All three build from the same install layout: `/usr/bin/{FeatherRPC,featherrpc}`,
`/usr/share/applications/featherrpc.desktop`, `/usr/share/icons/hicolor/`,
and a `featherrpc.service` user unit (except AppImage).
