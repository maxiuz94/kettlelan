#!/usr/bin/env bash
# KettleLAN – Windows installer: build with MSVC and Qt, collect the program, then pack it with Inno Setup.
# Runs in Git Bash (GitHub Actions windows runner).
#   packaging/windows/build-installer.sh <build-dir> <steamworks_sdk_XXX.zip>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:?build directory}"
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)" # absolute: Inno Setup resolves relative paths from the script's folder
SDK_ZIP="${2:?Steamworks SDK zip}"
VERSION="$(sed -n 's/^project(KettleLAN VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="KettleLAN-${VERSION}-windows-x64"
DIST="$BUILD/package" # what the installer puts into Program Files\KettleLAN

cmake -S "$ROOT" -B "$BUILD" -G "Visual Studio 17 2022" -A x64 -DSTEAMWORKS_SDK_ZIP="$SDK_ZIP"
cmake --build "$BUILD" --config Release --parallel
ctest --test-dir "$BUILD" -C Release --output-on-failure

rm -rf "$DIST" && mkdir -p "$DIST/licenses"
OUT="$BUILD/Release"
cp "$OUT/KettleLAN.exe" "$OUT/kettlelan-cli.exe" "$OUT/kettlelan-service.exe" "$OUT/steam_api64.dll" "$OUT/wintun.dll" "$DIST/"


# Qt libraries, QML modules, plugins and the Visual C++ runtime
windeployqt --release --qmldir "$ROOT/src/gui/qml" --no-translations --compiler-runtime "$DIST/KettleLAN.exe"
# KettleLAN uses only the "Basic" style (QQuickStyle::setStyle) – drop the others windeployqt adds
for style in Material Universal Fusion Imagine FluentWinUI3 Windows macOS iOS; do
    rm -rf "$DIST/qml/QtQuick/Controls/$style"
    rm -f "$DIST"/Qt6QuickControls2"$style".dll "$DIST"/Qt6QuickControls2"$style"StyleImpl.dll
done
echo "Files in the package: $(find "$DIST" -type f | wc -l)"

# licenses of everything in the package
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$DIST/"
cp "$ROOT"/packaging/licenses/*.txt "$DIST/licenses/"
cp "$BUILD/_deps/wintun/wintun/LICENSE.txt" "$DIST/licenses/Wintun-LICENSE.txt"
(cd "$DIST" && find . -type f \( -name '*.dll' -o -name '*.exe' \) | sed 's|^\./||' | sort) > "$DIST/licenses/bundled-files.txt"


# Inno Setup: preinstalled on older GitHub runner images, installed here when missing
ISCC="${ISCC:-/c/Program Files (x86)/Inno Setup 6/ISCC.exe}"
if [ ! -x "$ISCC" ]; then
    choco install innosetup --no-progress -y > /dev/null
fi
mkdir -p "$ROOT/dist"
# Git Bash would turn "/DName=…" options into paths – switch that off for this call only
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" "$ISCC" /Q "/DAppVersion=$VERSION" "/DSourceDir=$(cygpath -w "$DIST")" \
    "/DOutputDir=$(cygpath -w "$ROOT/dist")" "$(cygpath -w "$ROOT/packaging/windows/kettlelan.iss")"
echo "Done: dist/$NAME-setup.exe"
