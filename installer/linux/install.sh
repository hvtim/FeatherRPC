#!/usr/bin/env bash
set -euo pipefail

# Bump this alongside each release tag.
APP_VERSION="0.1.0"
EXE_NAME="FeatherRPC"

NO_AUTOSTART=0
for arg in "$@"; do
    case "$arg" in
        --no-autostart) NO_AUTOSTART=1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/app"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
INSTALL_DIR="$DATA_HOME/FeatherRPC"

if [ ! -d "$SOURCE_DIR" ]; then
    echo "Could not find the app files next to this script (expected '$SOURCE_DIR')." >&2
    echo "Make sure you extracted the whole release tarball before running install.sh." >&2
    exit 1
fi

echo "Installing FeatherRPC to $INSTALL_DIR ..."
mkdir -p "$INSTALL_DIR"

# A running instance holds the exe file open the same way the Windows and
# macOS builds do - stop it before copying over it, not just at the end.
pkill -x "$EXE_NAME" 2>/dev/null || true
sleep 0.3

cp -f "$SOURCE_DIR/$EXE_NAME" "$INSTALL_DIR/$EXE_NAME"
chmod +x "$INSTALL_DIR/$EXE_NAME"
cp -f "$SCRIPT_DIR/uninstall.sh" "$INSTALL_DIR/uninstall.sh"
chmod +x "$INSTALL_DIR/uninstall.sh"

# featherrpc-cli goes in ~/.local/bin, not $INSTALL_DIR - it's meant to be
# typed (`featherrpc-cli status`), and ~/.local/bin is on $PATH by default
# on most systemd-based distros, unlike $XDG_DATA_HOME.
if [ -f "$SOURCE_DIR/featherrpc-cli" ]; then
    mkdir -p "$HOME/.local/bin"
    cp -f "$SOURCE_DIR/featherrpc-cli" "$HOME/.local/bin/featherrpc-cli"
    chmod +x "$HOME/.local/bin/featherrpc-cli"

    # Generated with the real resolved install path baked in, rather than
    # shipping a static unit with a guessed path - $XDG_DATA_HOME isn't
    # always ~/.local/share.
    SYSTEMD_USER_DIR="$CONFIG_HOME/systemd/user"
    mkdir -p "$SYSTEMD_USER_DIR"
    cat > "$SYSTEMD_USER_DIR/featherrpcd.service" <<EOF
[Unit]
Description=FeatherRPC headless daemon (Discord Rich Presence from MPRIS)

[Service]
ExecStart=$INSTALL_DIR/$EXE_NAME --no-tray
Restart=on-failure

[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload 2>/dev/null || true
fi

if [ -f "$SOURCE_DIR/icon.png" ]; then
    ICON_DIR="$DATA_HOME/icons/hicolor/256x256/apps"
    mkdir -p "$ICON_DIR"
    cp -f "$SOURCE_DIR/icon.png" "$ICON_DIR/featherrpc.png"
    gtk-update-icon-cache "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
fi

APPLICATIONS_DIR="$DATA_HOME/applications"
mkdir -p "$APPLICATIONS_DIR"
cat > "$APPLICATIONS_DIR/FeatherRPC.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=FeatherRPC
Comment=Now-playing sync for Discord Rich Presence
Exec="$INSTALL_DIR/$EXE_NAME"
Icon=featherrpc
Categories=Utility;
EOF

if [ "$NO_AUTOSTART" -eq 0 ]; then
    # Same path/filename the app's own tray-menu "start at login" toggle
    # manages (see platform/linux/DesktopAutoLaunch.cpp) - toggling it off
    # and back on later just overwrites this same file, no conflict.
    AUTOSTART_DIR="$CONFIG_HOME/autostart"
    mkdir -p "$AUTOSTART_DIR"
    cat > "$AUTOSTART_DIR/FeatherRPC.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=FeatherRPC
Comment=Now-playing sync for Discord Rich Presence
Exec="$INSTALL_DIR/$EXE_NAME"
Icon=featherrpc
X-GNOME-Autostart-enabled=true
EOF
fi

if [ "$NO_AUTOSTART" -eq 0 ]; then
    echo "Installed. FeatherRPC will now start automatically at login."
else
    echo "Installed. Autostart at login was skipped - enable it anytime from the tray menu."
fi
echo "Added an application menu entry."
echo ""

pkill -x "$EXE_NAME" 2>/dev/null || true
sleep 0.3
nohup "$INSTALL_DIR/$EXE_NAME" >/dev/null 2>&1 &
disown

echo "Started FeatherRPC. A tray icon should now appear - right-click it to enter your Discord Application ID."
