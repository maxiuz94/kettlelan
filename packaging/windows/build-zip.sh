#!/usr/bin/env bash
# KettleLAN – Windows package: build with MSVC and Qt, then collect everything into one zip file.
# Runs in Git Bash (GitHub Actions windows runner).
#   packaging/windows/build-zip.sh <build-dir> <steamworks_sdk_XXX.zip>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:?build directory}"
SDK_ZIP="${2:?Steamworks SDK zip}"
VERSION="$(sed -n 's/^project(KettleLAN VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="KettleLAN-${VERSION}-windows-x64"
DIST="$ROOT/dist/$NAME"

cmake -S "$ROOT" -B "$BUILD" -G "Visual Studio 17 2022" -A x64 -DSTEAMWORKS_SDK_ZIP="$SDK_ZIP"
cmake --build "$BUILD" --config Release --parallel
ctest --test-dir "$BUILD" -C Release --output-on-failure

rm -rf "$DIST" && mkdir -p "$DIST/licenses"
OUT="$BUILD/Release"
cp "$OUT/KettleLAN.exe" "$OUT/kettlelan-cli.exe" "$OUT/steam_api64.dll" "$OUT/wintun.dll" "$DIST/"

# Qt libraries, QML modules, plugins and the Visual C++ runtime
windeployqt --release --qmldir "$ROOT/src/gui/qml" --no-translations --compiler-runtime "$DIST/KettleLAN.exe"

# licenses of everything in the package
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$DIST/"
cp "$ROOT"/packaging/licenses/*.txt "$DIST/licenses/"
cp "$BUILD/_deps/wintun/wintun/LICENSE.txt" "$DIST/licenses/Wintun-LICENSE.txt"
(cd "$DIST" && find . -type f \( -name '*.dll' -o -name '*.exe' \) | sed 's|^\./||' | sort) > "$DIST/licenses/bundled-files.txt"

cat > "$DIST/README.txt" << README
KettleLAN ${VERSION} for Windows 10/11 (64-bit)
Open-source virtual LAN over the Steam network.

1. Start Steam and sign in.
2. Run KettleLAN.exe. Windows asks for administrator rights - they are needed to create
   the virtual network adapter (Wintun).
3. If Windows SmartScreen shows "Windows protected your PC", click "More info" and "Run anyway"
   (the program is not code-signed yet).

Instructions, source code and security information: https://github.com/maxiuz94/kettlelan
Licenses of KettleLAN and the bundled components: LICENSE, THIRD-PARTY-NOTICES.md, licenses\\
README

mkdir -p "$ROOT/dist"
rm -f "$ROOT/dist/$NAME.zip"
(cd "$ROOT/dist" && 7z a -tzip -mx=9 "$NAME.zip" "$NAME" > /dev/null)
echo "Done: dist/$NAME.zip"
