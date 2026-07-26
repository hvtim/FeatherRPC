#!/usr/bin/env bash
# Deliberately no `set -e` - every step below should run best-effort even
# if an earlier one finds nothing to remove.

EXE_NAME="FeatherRPC"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
INSTALL_DIR="$DATA_HOME/FeatherRPC"

echo "Uninstalling FeatherRPC..."

pkill -x "$EXE_NAME" 2>/dev/null
sleep 0.3

SYSTEMD_UNIT="$CONFIG_HOME/systemd/user/featherrpcd.service"
if [ -f "$SYSTEMD_UNIT" ]; then
    systemctl --user disable --now featherrpcd.service 2>/dev/null
    rm -f "$SYSTEMD_UNIT"
    systemctl --user daemon-reload 2>/dev/null || true
    echo "Removed and disabled the featherrpcd systemd user unit."
fi

CLI_PATH="$HOME/.local/bin/featherrpc-cli"
if [ -f "$CLI_PATH" ]; then
    rm -f "$CLI_PATH"
    echo "Removed featherrpc-cli."
fi

AUTOSTART_FILE="$CONFIG_HOME/autostart/FeatherRPC.desktop"
if [ -f "$AUTOSTART_FILE" ]; then
    rm -f "$AUTOSTART_FILE"
    echo "Removed autostart entry."
fi

APPLICATIONS_FILE="$DATA_HOME/applications/FeatherRPC.desktop"
if [ -f "$APPLICATIONS_FILE" ]; then
    rm -f "$APPLICATIONS_FILE"
    echo "Removed application menu entry."
fi

ICON_FILE="$DATA_HOME/icons/hicolor/256x256/apps/featherrpc.png"
if [ -f "$ICON_FILE" ]; then
    rm -f "$ICON_FILE"
    gtk-update-icon-cache "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
fi

if [ -d "$INSTALL_DIR" ]; then
    # This script is also shipped inside $INSTALL_DIR (so it keeps working
    # without the original release tarball) - unlike Windows, deleting a
    # running script's own file on Linux just unlinks the directory entry;
    # the shell keeps the inode open via its own fd and finishes normally,
    # so no deferred-process trick is needed here.
    rm -rf "$INSTALL_DIR"
    echo "Removed installed files from $INSTALL_DIR."
fi

echo ""
echo "FeatherRPC has been fully uninstalled."
echo "Nothing else on this machine was changed - Discord and your media players are untouched."
