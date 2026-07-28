#!/bin/bash
# Fully removes FeatherRPC: running process, LaunchAgent, and the
# installed .app bundle. Mirrors the Windows uninstaller's scope.
set -uo pipefail

APP_NAME="FeatherRPC.app"
INSTALL_DIR="$HOME/Applications"
LABEL="com.hvtim.featherrpc"
PLIST_PATH="$HOME/Library/LaunchAgents/$LABEL.plist"

echo "Uninstalling FeatherRPC..."

pkill -f "$INSTALL_DIR/$APP_NAME" 2>/dev/null || true
sleep 0.3

if [ -f "$PLIST_PATH" ]; then
    launchctl unload "$PLIST_PATH" 2>/dev/null || true
    rm -f "$PLIST_PATH"
    echo "Removed autostart LaunchAgent."
fi

if [ -d "$INSTALL_DIR/$APP_NAME" ]; then
    rm -rf "$INSTALL_DIR/$APP_NAME"
    echo "Removed $INSTALL_DIR/$APP_NAME."
fi

CLI_PATH="$HOME/.local/bin/featherrpc-cli"
if [ -f "$CLI_PATH" ]; then
    rm -f "$CLI_PATH"
    echo "Removed featherrpc-cli."
fi

CLI_ADAPTER_DIR="$HOME/.local/bin/mediaremote-adapter"
if [ -d "$CLI_ADAPTER_DIR" ]; then
    rm -rf "$CLI_ADAPTER_DIR"
    echo "Removed featherrpc-cli's bundled mediaremote-adapter."
fi

echo ""
echo "FeatherRPC has been fully uninstalled."
echo "Nothing else on this machine was changed - Discord and Music.app themselves are untouched."
