#!/usr/bin/env bash
# KettleLAN – build on Linux. Usage: ./scripts/build.sh [extra cmake options]
set -euo pipefail
cd "$(dirname "$0")/.."

SDK_URL="https://partner.steamgames.com/downloads/list"

find_sdk_zip() {
    local d
    for d in "${XDG_DOWNLOAD_DIR:-}" "$HOME/Downloads" "$HOME/Pobrane" "$PWD"; do
        [ -n "$d" ] && ls "$d"/steamworks_sdk_*.zip 2>/dev/null | sort -V | tail -n1
    done | tail -n1
}

have_sdk() {
    [ -f build/_deps/steamworks/sdk/public/steam/steam_api.h ] || [ -n "$(find_sdk_zip)" ]
}

if ! have_sdk && [[ " $* " != *"STEAMWORKS_SDK_"* ]]; then
    echo "Building needs the Steamworks SDK (free, requires a Steam account)."
    echo "Ready-made KettleLAN packages do not need it – only building from source does."
    read -rp "Open the download page in your browser? [Y/n] " answer
    if [[ ! "$answer" =~ ^[nN] ]]; then
        (xdg-open "$SDK_URL" >/dev/null 2>&1 &) || echo "Open it manually: $SDK_URL"
    fi
    echo
    echo "1. Sign in with your Steam account and accept the license."
    echo "2. Download the latest \"Steamworks SDK\" (steamworks_sdk_XXX.zip) to your Downloads folder."
    until have_sdk; do
        read -rp "Press Enter when the download has finished (Ctrl+C to cancel)… " _
        have_sdk || echo "Still no steamworks_sdk_*.zip in your Downloads folder."
    done
    echo "Found: $(find_sdk_zip)"
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --parallel "${JOBS:-$(nproc)}"   # JOBS=2 on low-memory computers
ctest --test-dir build --output-on-failure

echo
echo "Done."
echo "  ./build/kettlelan                the application window"
echo "  ./build/kettlelan-cli            command-line version (diagnostics)"
echo "  ./scripts/install-local.sh       adds KettleLAN to your application menu"
