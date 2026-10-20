#!/usr/bin/env bash
# KettleLAN – budowanie na Linuksie. Użycie: ./scripts/build.sh [dodatkowe opcje cmake]
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
    echo "Do kompilacji potrzebne jest Steamworks SDK (darmowe, wymaga konta Steam)."
    echo "Gotowe paczki KettleLAN go nie potrzebują – tylko budowanie ze źródeł."
    read -rp "Otworzyć stronę pobierania w przeglądarce? [T/n] " answer
    if [[ ! "$answer" =~ ^[nN] ]]; then
        (xdg-open "$SDK_URL" >/dev/null 2>&1 &) || echo "Otwórz ręcznie: $SDK_URL"
    fi
    echo
    echo "1. Zaloguj się kontem Steam i zaakceptuj licencję."
    echo "2. Pobierz najnowsze „Steamworks SDK” (plik steamworks_sdk_XXX.zip) do folderu Pobrane."
    until have_sdk; do
        read -rp "Naciśnij Enter, gdy pobieranie się zakończy (Ctrl+C przerywa)… " _
        have_sdk || echo "Nadal nie widzę pliku steamworks_sdk_*.zip w Pobranych."
    done
    echo "Znaleziono: $(find_sdk_zip)"
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

echo
echo "Gotowe. Uruchom:  ./build/kettlelan"
