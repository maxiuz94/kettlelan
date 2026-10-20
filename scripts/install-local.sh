#!/usr/bin/env bash
# KettleLAN – instalacja dla bieżącego użytkownika (bez roota), z ikoną w menu aplikacji.
#   ./scripts/install-local.sh            instaluje zbudowany program z folderu build/
#   ./scripts/install-local.sh --remove   usuwa instalację
set -euo pipefail
cd "$(dirname "$0")/.."

DEST="$HOME/.local/opt/kettlelan"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"

if [ "${1:-}" = "--remove" ]; then
    rm -rf "$DEST" "$APPS/kettlelan.desktop" "$ICONS/kettlelan.png"
    echo "Usunięto KettleLAN z $DEST i z menu aplikacji."
    exit 0
fi

[ -x build/kettlelan ] || { echo "Najpierw zbuduj program: ./scripts/build.sh"; exit 1; }

mkdir -p "$DEST" "$APPS" "$ICONS"
cp build/kettlelan build/kettlelan-cli build/libsteam_api.so build/kettlelan-helper "$DEST/"
QT_QPA_PLATFORM=offscreen "$DEST/kettlelan" --write-icon "$ICONS/kettlelan.png"

cat > "$APPS/kettlelan.desktop" << DESKTOP
[Desktop Entry]
Type=Application
Name=KettleLAN
GenericName=Wirtualny LAN
Comment=Graj ze znajomymi w gry LAN przez sieć Steam
Exec="$DEST/kettlelan"
Icon=kettlelan
Terminal=false
Categories=Network;Game;
Keywords=lan;vpn;hamachi;steam;multiplayer;
StartupWMClass=kettlelan
DESKTOP

update-desktop-database "$APPS" >/dev/null 2>&1 || true
echo "Zainstalowano w $DEST. KettleLAN jest w menu aplikacji (kategoria Internet / Gry)."
