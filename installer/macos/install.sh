#!/bin/bash
# Installs FeatherRPC.app to ~/Applications (per-user, no admin/sudo
# required - same no-admin philosophy as the Windows installer's
# %LOCALAPPDATA% install). Run from the extracted release folder, next to
# the built FeatherRPC.app bundle.
set -euo pipefail

APP_NAME="FeatherRPC.app"
INSTALL_DIR="$HOME/Applications"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_APP="$SCRIPT_DIR/$APP_NAME"
LABEL="com.hvtim.featherrpc"
PLIST_PATH="$HOME/Library/LaunchAgents/$LABEL.plist"

NO_AUTOSTART=0
for arg in "$@"; do
    if [ "$arg" = "--no-autostart" ]; then
        NO_AUTOSTART=1
    fi
done

if [ ! -d "$SOURCE_APP" ]; then
    echo "Could not find $APP_NAME next to this script (expected $SOURCE_APP)." >&2
    echo "Make sure you extracted the whole release, and built/copied the .app bundle here." >&2
    exit 1
fi

echo "Installing FeatherRPC to $INSTALL_DIR/$APP_NAME ..."
mkdir -p "$INSTALL_DIR"

# Stop any already-running instance before copying over it - matches the
# Windows installer's fix for the same "file in use" failure mode.
pkill -f "$INSTALL_DIR/$APP_NAME" 2>/dev/null || true
sleep 0.3

rm -rf "$INSTALL_DIR/$APP_NAME"
cp -R "$SOURCE_APP" "$INSTALL_DIR/$APP_NAME"

EXE_PATH="$INSTALL_DIR/$APP_NAME/Contents/MacOS/FeatherRPC"

# featherrpc-cli is a plain command-line tool, not part of the .app bundle
# - installed to ~/.local/bin, matching the Linux installer's choice (may
# need adding to $PATH manually on macOS, unlike most Linux distros).
if [ -f "$SCRIPT_DIR/featherrpc-cli" ]; then
    mkdir -p "$HOME/.local/bin"
    cp -f "$SCRIPT_DIR/featherrpc-cli" "$HOME/.local/bin/featherrpc-cli"
    chmod +x "$HOME/.local/bin/featherrpc-cli"

    # `mediasource current` needs the adapter bundled next to the CLI
    # binary (see the featherrpc-cli POST_BUILD step in native/CMakeLists.txt)
    # - it's not part of the .app bundle, so it doesn't get this for free
    # from Contents/Resources like the GUI target does.
    if [ -d "$SCRIPT_DIR/mediaremote-adapter" ]; then
        rm -rf "$HOME/.local/bin/mediaremote-adapter"
        cp -R "$SCRIPT_DIR/mediaremote-adapter" "$HOME/.local/bin/mediaremote-adapter"
    fi
fi

if [ "$NO_AUTOSTART" -eq 0 ]; then
    mkdir -p "$HOME/Library/LaunchAgents"
    cat > "$PLIST_PATH" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$EXE_PATH</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
</dict>
</plist>
PLIST
    echo "Installed. FeatherRPC will now start automatically at login."
else
    echo "Installed. Autostart at login was skipped - enable it anytime from the tray menu."
fi

open "$INSTALL_DIR/$APP_NAME"
echo "Started FeatherRPC. A menu bar icon should now appear - click it to enter your Discord Application ID."
