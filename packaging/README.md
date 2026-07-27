# Linux distro packaging

- **[aur/](aur/)** - AUR `PKGBUILD`. Not yet submitted - pending an AUR
  account and a real `v0.1.1` tag to build against.
- **[copr/](copr/)** - Fedora COPR `.spec`. Verified locally with a real
  `rpmbuild`/`dnf` install; not yet submitted to COPR itself.
- **[appimage/](appimage/)** - `build-appimage.sh` produces a portable
  single-file AppImage. Built and bundle-verified; real-desktop run
  confirmation pending (AppImages don't run reliably under WSL).

All three build from the same install layout: `/usr/bin/{FeatherRPC,featherrpc}`,
`/usr/share/applications/featherrpc.desktop`, `/usr/share/icons/hicolor/`,
and (except AppImage, which has no systemd of its own) a `featherrpc.service`
user unit.
