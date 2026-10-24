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
cp "$OUT/KettleLAN.exe" "$OUT/kettlelan-cli.exe" "$OUT/kettlelan-service.exe" "$OUT/steam_api64.dll" "$OUT/wintun.dll" "$DIST/"
# until the installer: install / remove the service by hand (right click → Run as administrator)
printf '@echo off\r\n"%%~dp0kettlelan-service.exe" install\r\npause\r\n' > "$DIST/install-service.cmd"
printf '@echo off\r\n"%%~dp0kettlelan-service.exe" uninstall\r\npause\r\n' > "$DIST/uninstall-service.cmd"

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

1. Right click install-service.cmd and choose "Run as administrator" (once). This installs
   "KettleLAN Service", which runs the virtual network adapter (Wintun) in the background.
2. Start Steam and sign in, then run KettleLAN.exe – no administrator rights needed.
3. If Windows SmartScreen shows "Windows protected your PC", click "More info" and "Run anyway"
   (the program is not code-signed yet).

To remove KettleLAN: run uninstall-service.cmd as administrator, then delete this folder.

Download KettleLAN only from the official page: https://github.com/maxiuz94/kettlelan/releases
Copies from other websites may be modified. How to verify a download:
https://github.com/maxiuz94/kettlelan/blob/main/SECURITY.md#verifying-a-download

Instructions, source code and security information: https://github.com/maxiuz94/kettlelan
Licenses of KettleLAN and the bundled components: LICENSE, THIRD-PARTY-NOTICES.md, licenses\\
README

mkdir -p "$ROOT/dist"
rm -f "$ROOT/dist/$NAME.zip"
(cd "$ROOT/dist" && 7z a -tzip -mx=9 "$NAME.zip" "$NAME" > /dev/null)
echo "Done: dist/$NAME.zip"
