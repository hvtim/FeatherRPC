#!/bin/bash
# Packages an already-built FeatherRPC.app into a distributable .dmg with the
# standard drag-to-/Applications layout: the .app and an Applications symlink
# side by side, both visible, in a Finder window sized and arranged via
# AppleScript. Run this after `cmake --build` produces FeatherRPC.app - it
# does not build anything itself.
#
# WRITTEN, NOT VERIFIED: there has never been Mac hardware anywhere in this
# project's development environment. hdiutil, osascript, and Finder scripting
# behavior here are based on documented behavior and the conventional pattern
# widely used by other open source Mac apps' release scripts, not on an
# actual run. Treat this the same as the rest of the macOS port: believed
# correct, unverified. A maintainer with real Mac hardware should run this
# once and fix whatever's actually wrong before relying on it for a release.
#
# Does NOT sign or notarize anything - that needs an Apple Developer account
# and real hardware, both out of scope here. See installer/macos/README.md
# for what that means for anyone downloading the resulting .dmg (Gatekeeper
# will block first launch).
#
# Usage:
#   build-dmg.sh --app path/to/FeatherRPC.app [options]
#
# Options:
#   --app PATH           Path to the built .app bundle (required)
#   --output PATH        Output .dmg path (default: ./<volname>.dmg)
#   --volname NAME       Volume name shown when mounted (default: .app's
#                        bundle name, e.g. "FeatherRPC")
#   --background PATH    Optional PNG/TIFF background image for the Finder
#                        window. Skipped (plain background) if omitted.
#   --no-finder-layout   Skip the osascript/Finder styling step entirely and
#                        just produce a plain compressed .dmg with the same
#                        contents. Use this if osascript fails because
#                        there's no logged-in GUI session (some headless CI
#                        runners) - the .dmg still works, it just opens as an
#                        unstyled Finder window instead of the drag-install
#                        layout.
#   -h, --help           Show this help and exit
set -euo pipefail

APP_PATH=""
OUTPUT_PATH=""
VOLUME_NAME=""
BACKGROUND_PATH=""
SKIP_FINDER_LAYOUT=0

usage() {
    sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --app) APP_PATH="$2"; shift 2 ;;
        --output) OUTPUT_PATH="$2"; shift 2 ;;
        --volname) VOLUME_NAME="$2"; shift 2 ;;
        --background) BACKGROUND_PATH="$2"; shift 2 ;;
        --no-finder-layout) SKIP_FINDER_LAYOUT=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

if [ "$(uname -s)" != "Darwin" ]; then
    echo "This script uses hdiutil and osascript, both macOS-only. Run it on a Mac." >&2
    exit 1
fi

if [ -z "$APP_PATH" ]; then
    echo "Missing --app <path to FeatherRPC.app>" >&2
    usage
    exit 1
fi

if [ ! -d "$APP_PATH" ] || [ ! -f "$APP_PATH/Contents/Info.plist" ]; then
    echo "$APP_PATH does not look like a .app bundle (no Contents/Info.plist)." >&2
    exit 1
fi

APP_PATH="$(cd "$(dirname "$APP_PATH")" && pwd)/$(basename "$APP_PATH")"
APP_BASENAME="$(basename "$APP_PATH")"          # e.g. FeatherRPC.app
APP_NAME="${APP_BASENAME%.app}"                  # e.g. FeatherRPC

if [ -z "$VOLUME_NAME" ]; then
    VOLUME_NAME="$APP_NAME"
fi

if [ -z "$OUTPUT_PATH" ]; then
    OUTPUT_PATH="./${APP_NAME}.dmg"
fi
mkdir -p "$(dirname "$OUTPUT_PATH")"
OUTPUT_PATH="$(cd "$(dirname "$OUTPUT_PATH")" && pwd)/$(basename "$OUTPUT_PATH")"

if [ -n "$BACKGROUND_PATH" ] && [ ! -f "$BACKGROUND_PATH" ]; then
    echo "Background image not found: $BACKGROUND_PATH" >&2
    exit 1
fi

WORK_DIR="$(mktemp -d /tmp/featherrpc-dmg.XXXXXX)"
STAGING_DIR="$WORK_DIR/staging"
RW_DMG="$WORK_DIR/rw.dmg"
MOUNT_DIR="/Volumes/$VOLUME_NAME"
DEVICE=""

cleanup() {
    # Best-effort - a failed detach here shouldn't mask the real error, and
    # on a genuinely dry run (e.g. --help, already exited above) there's
    # nothing to clean up.
    if [ -n "$DEVICE" ]; then
        hdiutil detach "$DEVICE" -quiet 2>/dev/null || true
    fi
    rm -rf "$WORK_DIR" 2>/dev/null || true
}
trap cleanup EXIT

echo "Staging DMG contents..."
mkdir -p "$STAGING_DIR"

# ditto (not cp -R) preserves resource forks, extended attributes, and an
# existing code signature intact - cp -R can silently drop or mangle these
# on a .app bundle.
ditto "$APP_PATH" "$STAGING_DIR/$APP_BASENAME"

# The conventional drag-to-install symlink target is the system-wide
# /Applications folder (distinct from install.sh, which installs to
# ~/Applications for the no-admin case - the two install paths are
# independent, see installer/macos/README.md). Dropping the .app here
# requires admin only in the same way any drag-to-/Applications copy does;
# it is not a per-file permission this script controls.
ln -s /Applications "$STAGING_DIR/Applications"

BACKGROUND_FILENAME=""
if [ -n "$BACKGROUND_PATH" ]; then
    mkdir -p "$STAGING_DIR/.background"
    BACKGROUND_FILENAME="$(basename "$BACKGROUND_PATH")"
    cp "$BACKGROUND_PATH" "$STAGING_DIR/.background/$BACKGROUND_FILENAME"
fi

# Size the RW image with headroom over the staged content (hdiutil create
# -srcfolder auto-sizes fine without -size too, but padding avoids "not
# enough space" failures if Finder writes .DS_Store/icon caches into the
# volume before it's converted down).
CONTENT_MB=$(du -sm "$STAGING_DIR" | cut -f1)
IMAGE_MB=$((CONTENT_MB + 20))

echo "Creating read-write DMG..."
rm -f "$RW_DMG"
hdiutil create -volname "$VOLUME_NAME" -srcfolder "$STAGING_DIR" -ov \
    -fs HFS+ -format UDRW -size "${IMAGE_MB}m" "$RW_DMG"

if [ -d "$MOUNT_DIR" ]; then
    echo "A volume named '$VOLUME_NAME' is already mounted at $MOUNT_DIR - detach it and re-run." >&2
    exit 1
fi

echo "Mounting for layout..."
# hdiutil attach's own output lists one line per partition slice; the first
# /dev/ line is the whole disk device (e.g. /dev/disk4), which is what
# hdiutil detach expects to take down every slice/mount at once.
ATTACH_OUTPUT=$(hdiutil attach "$RW_DMG" -readwrite -noverify -noautoopen)
DEVICE=$(echo "$ATTACH_OUTPUT" | grep -E '^/dev/' | sed -n '1p' | awk '{print $1}')
if [ -z "$DEVICE" ]; then
    echo "Could not determine device from hdiutil attach output:" >&2
    echo "$ATTACH_OUTPUT" >&2
    exit 1
fi

# Wait for the mount to actually appear before scripting it.
for _ in $(seq 1 20); do
    [ -d "$MOUNT_DIR" ] && break
    sleep 0.5
done
if [ ! -d "$MOUNT_DIR" ]; then
    echo "Volume never appeared at $MOUNT_DIR after attach - aborting." >&2
    exit 1
fi

if [ "$SKIP_FINDER_LAYOUT" -eq 0 ]; then
    echo "Styling Finder window (osascript)..."
    # Standard AppleScript pattern used by most indie Mac apps' dmg build
    # scripts (e.g. the widely-used create-dmg tool): open the volume as a
    # Finder window, force icon view with fixed positions, optionally set a
    # background picture, then close/reopen once to make the size stick
    # before the window state is written to the volume's .DS_Store.
    #
    # Requires a logged-in GUI session with Finder running - will fail on a
    # true headless runner. Not fatal to the build: caught below, falls back
    # to a plain (unstyled but functional) DMG. Re-run with
    # --no-finder-layout to skip this step outright instead of racing it.
    if ! osascript <<APPLESCRIPT
tell application "Finder"
    tell disk "$VOLUME_NAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {400, 120, 940, 500}
        set theViewOptions to the icon view options of container window
        set arrangement of theViewOptions to not arranged
        set icon size of theViewOptions to 128
        set text size of theViewOptions to 12
        try
            if "$BACKGROUND_FILENAME" is not "" then
                set background picture of theViewOptions to file ".background:$BACKGROUND_FILENAME"
            end if
        end try
        set position of item "$APP_BASENAME" of container window to {150, 190}
        set position of item "Applications" of container window to {390, 190}
        close
        open
        update without registering applications
        delay 1
    end tell
end tell
APPLESCRIPT
    then
        echo "Warning: Finder styling failed (osascript). Continuing with a plain DMG layout - the .app and Applications symlink are still both there, just unstyled." >&2
    fi
fi

sync
echo "Detaching..."
hdiutil detach "$DEVICE" -quiet
DEVICE=""

echo "Compressing to final DMG..."
rm -f "$OUTPUT_PATH"
hdiutil convert "$RW_DMG" -format UDZO -imagekey zlib-level=9 -ov -o "$OUTPUT_PATH"

echo "Done: $OUTPUT_PATH"
echo "This DMG is unsigned and not notarized - see installer/macos/README.md" \
     "for what that means for anyone who downloads it."
