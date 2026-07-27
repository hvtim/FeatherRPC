#!/usr/bin/env bash
# Builds FeatherRPC-<version>-x86_64.AppImage from an already-built
# native/ tree. Run this after building normally:
#
#   cd native && cmake -B build && cmake --build build
#   packaging/appimage/build-appimage.sh native/build
#
# Requires curl and a Fedora/Arch-style desktop (fuse2 or the kernel's
# built-in FUSE for AppImage's own extraction step - APPIMAGE_EXTRACT_AND_RUN
# below sidesteps needing FUSE at all).
set -euo pipefail

BUILD_DIR="$(cd "${1:-native/build}" && pwd)"
VERSION="${VERSION:-0.1.1}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

if [ ! -x "$BUILD_DIR/FeatherRPC" ] || [ ! -x "$BUILD_DIR/featherrpc" ]; then
    echo "error: $BUILD_DIR doesn't contain built FeatherRPC/featherrpc binaries" >&2
    exit 1
fi

# Everything below runs from $WORK_DIR - linuxdeploy's --appimage-extract
# and the final --output appimage both write relative to the current
# directory, not wherever the tool itself lives.
cd "$WORK_DIR"

echo "Fetching linuxdeploy..."
curl -sL -o "$WORK_DIR/linuxdeploy.AppImage" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x "$WORK_DIR/linuxdeploy.AppImage"

# linuxdeploy's own bundled `strip` is too old to parse the RELR
# relocations current-generation toolchains emit (fails with "unknown
# type [0x13] section .relr.dyn" and aborts the whole build) - swap in
# the host's own strip, which built these binaries in the first place
# and is guaranteed compatible.
"$WORK_DIR/linuxdeploy.AppImage" --appimage-extract > /dev/null
cp "$(command -v strip)" "$WORK_DIR/squashfs-root/usr/bin/strip"

APPDIR="$WORK_DIR/AppDir"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$BUILD_DIR/FeatherRPC" "$APPDIR/usr/bin/FeatherRPC"
cp "$BUILD_DIR/featherrpc" "$APPDIR/usr/bin/featherrpc"
cp "$REPO_ROOT/installer/linux/app/icon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/featherrpc.png" 2>/dev/null \
    || cp "$REPO_ROOT/assets/icon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/featherrpc.png"
cp "$REPO_ROOT/packaging/aur/featherrpc.desktop" "$APPDIR/usr/share/applications/featherrpc.desktop"

echo "Running linuxdeploy..."
# Excluding every bundled library, not just the system-integration ones
# (libudev/libsystemd/libselinux/libmount/libblkid) that seemed like the
# obviously unsafe ones to bundle. Confirmed live: excluding only those
# fixed libudev specifically, but the same dynamic-linker-init segfault
# immediately reappeared in a different bundled library (libcbor, then
# libcrypto after excluding a much longer list) - this build's system
# libraries use RELR-packed relocations that don't survive being loaded
# from a relocated AppImage path, and that's not specific to any one
# library. Our actual runtime deps (glib2/dbus/curl) are near-universal
# on desktop Linux already, so relying on the target system's own copies
# for all of them isn't the portability loss it would have been with the
# old GTK/appindicator dependency footprint. See docs/KnownIssues.md.
VERSION="$VERSION" "$WORK_DIR/squashfs-root/AppRun" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/FeatherRPC" \
    --executable "$APPDIR/usr/bin/featherrpc" \
    --desktop-file "$APPDIR/usr/share/applications/featherrpc.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/featherrpc.png" \
    --exclude-library='*' \
    --output appimage

mv "$WORK_DIR"/FeatherRPC-*.AppImage "$REPO_ROOT/"
echo "Built: $REPO_ROOT/FeatherRPC-$VERSION-x86_64.AppImage"
