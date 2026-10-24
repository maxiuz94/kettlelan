#!/usr/bin/env bash
# KettleLAN – install for the current user (no root), with an entry in the application menu.
#   ./scripts/install-local.sh            installs the program built in build/
#   ./scripts/install-local.sh --remove   removes the installation
set -euo pipefail
cd "$(dirname "$0")/.."

DEST="$HOME/.local/opt/kettlelan"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"

if [ "${1:-}" = "--remove" ]; then
    rm -rf "$DEST" "$APPS/kettlelan.desktop" "$ICONS/kettlelan.png"
    echo "Removed KettleLAN from $DEST and from the application menu."
    exit 0
fi

[ -x build/kettlelan ] || { echo "Build the program first: ./scripts/build.sh"; exit 1; }

mkdir -p "$DEST" "$APPS" "$ICONS"
cp --remove-destination build/kettlelan build/kettlelan-steam build/kettlelan-cli build/libsteam_api.so build/kettlelan-helper "$DEST/"
QT_QPA_PLATFORM=offscreen "$DEST/kettlelan" --write-icon "$ICONS/kettlelan.png"

cat > "$APPS/kettlelan.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=KettleLAN
GenericName=Virtual LAN
Comment=Play LAN games with friends over the Steam network
Exec="$DEST/kettlelan"
Icon=kettlelan
Terminal=false
Categories=Network;Game;
Keywords=lan;vpn;hamachi;steam;multiplayer;
StartupWMClass=kettlelan
DESKTOP

update-desktop-database "$APPS" >/dev/null 2>&1 || true
echo "Installed to $DEST. KettleLAN is in your application menu (Internet / Games)."
