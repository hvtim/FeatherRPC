#!/bin/bash
# Generates a .icns app icon from a source PNG at build time. iconutil and
# sips are macOS-only, so this only ever runs from CMakeLists.txt's APPLE
# branch - there's no cross-platform equivalent to fall back to, and none
# needed since this script never runs anywhere else.
#
# Usage: make-icon.sh <source.png> <output.icns>
set -euo pipefail

SRC="$1"
OUT="$2"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

ICONSET="$WORK_DIR/AppIcon.iconset"
mkdir -p "$ICONSET"

for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$SRC" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "$double" "$double" "$SRC" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done

iconutil -c icns "$ICONSET" -o "$OUT"
