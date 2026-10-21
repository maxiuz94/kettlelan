#!/usr/bin/env bash
# KettleLAN – building the AppImage (a single file with Qt inside).
#
#   QMAKE=/path/to/qmake6 packaging/linux/build-appimage.sh [build-dir]
#
# The linuxdeploy tools are downloaded in pinned versions and verified with SHA256.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-appimage}"
mkdir -p "$BUILD"
BUILD="$(cd "$BUILD" && pwd)"   # absolute path – the script changes directory later
TOOLS="$BUILD/tools"
VERSION="$(sed -n 's/^project(KettleLAN VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_SHA256="4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a"
QTPLUGIN_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage"
QTPLUGIN_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"

fetch() { # fetch <url> <sha256> <file>
    if [ ! -f "$3" ] || ! echo "$2  $3" | sha256sum -c --status; then
        curl -fsSL -o "$3" "$1"
        echo "$2  $3" | sha256sum -c || { echo "Wrong checksum: $3"; rm -f "$3"; exit 1; }
    fi
    chmod +x "$3"
}

mkdir -p "$TOOLS"
fetch "$LINUXDEPLOY_URL" "$LINUXDEPLOY_SHA256" "$TOOLS/linuxdeploy-x86_64.AppImage"
fetch "$QTPLUGIN_URL" "$QTPLUGIN_SHA256" "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DKETTLELAN_PACKAGE=appimage ${CMAKE_EXTRA_ARGS:-}
cmake --build "$BUILD" --parallel "${JOBS:-$(nproc)}"   # JOBS=2 on low-memory computers
ctest --test-dir "$BUILD" --output-on-failure

APPDIR="$BUILD/AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr

# QML used by the program (QtQuick, Controls, Layouts, Shapes… modules)
export QML_SOURCES_PATHS="$ROOT/src/gui/qml"
# Wayland next to X11 – only together with the shell integration plugins, without which Qt falls back to X11 anyway.
export QMAKE="${QMAKE:-qmake6}"
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
WAYLAND_ARGS=()
if [ -f "$QT_PLUGINS/platforms/libqwayland-generic.so" ] && [ -d "$QT_PLUGINS/wayland-shell-integration" ]; then
    export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
    for d in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
        if [ -d "$QT_PLUGINS/$d" ]; then
            mkdir -p "$APPDIR/usr/plugins/$d"
            cp -a "$QT_PLUGINS/$d/." "$APPDIR/usr/plugins/$d/"
            WAYLAND_ARGS+=(--deploy-deps-only "$APPDIR/usr/plugins/$d")
        fi
    done
    echo "Wayland: plugin and shell integrations included (${#WAYLAND_ARGS[@]} arguments)"
else
    echo "Wayland: no shell integrations in this Qt install – the AppImage will use X11 (XWayland)"
fi
export APPIMAGE_EXTRACT_AND_RUN=1   # without FUSE (e.g. on GitHub Actions)
export LDAI_OUTPUT="KettleLAN-${VERSION}-x86_64.AppImage"

cd "$BUILD"
# Pass 1: deploy the program, Qt and all needed libraries into the AppDir.
PATH="$TOOLS:$PATH" "$TOOLS/linuxdeploy-x86_64.AppImage" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/kettlelan" \
    --executable "$APPDIR/usr/bin/kettlelan-cli" \
    --desktop-file "$APPDIR/usr/share/applications/kettlelan.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/kettlelan.png" \
    "${WAYLAND_ARGS[@]}" \
    --plugin qt

# Licenses of everything inside the AppImage (LGPL and the libraries' own licenses require it).
DOC="$APPDIR/usr/share/doc/kettlelan"
mkdir -p "$DOC/licenses"
cp "$ROOT/LICENSE" "$ROOT/THIRD-PARTY-NOTICES.md" "$DOC/"
cp "$ROOT"/packaging/licenses/*.txt "$DOC/licenses/"
QT_VERSION="$("$QMAKE" -query QT_VERSION)"
QT_LIBS="$("$QMAKE" -query QT_INSTALL_LIBS)"
{
    echo "Libraries bundled in KettleLAN-${VERSION}-x86_64.AppImage (generated at build time)."
    echo "Format: file  <-  origin. License files: *.copyright in this folder; Qt: LGPL-3.0.txt."
    echo
    find "$APPDIR/usr/lib" "$APPDIR/usr/plugins" "$APPDIR/usr/bin" -name '*.so*' -type f 2>/dev/null | sort |
    while read -r f; do
        name="$(basename "$f")"
        rel="${f#"$APPDIR"/}"
        pkg="$(dpkg -S "*/$name" 2>/dev/null | head -n1 | cut -d: -f1 | cut -d, -f1 || true)"
        if [ "$name" = "libsteam_api.so" ]; then
            echo "$rel  <-  Steamworks SDK (Valve, Steamworks SDK Access Agreement)"
        elif [[ "$name" == libQt6* || "$rel" == usr/plugins/* || "$pkg" == libqt6* || "$pkg" == qt6-* || "$pkg" == qml6-* ]]; then
            echo "$rel  <-  Qt $QT_VERSION (LGPL-3.0)"
        elif [ -n "$pkg" ]; then
            echo "$rel  <-  package $pkg"
            [ -f "/usr/share/doc/$pkg/copyright" ] && cp "/usr/share/doc/$pkg/copyright" "$DOC/licenses/$pkg.copyright"
        elif [ -e "$QT_LIBS/$name" ]; then
            # shipped with the official Qt binaries (e.g. ICU) – covered by Qt's licensing documentation
            echo "$rel  <-  shipped with Qt $QT_VERSION"
        else
            echo "$rel  <-  unknown origin"
        fi
    done
} > "$DOC/licenses/bundled-libraries.txt"
echo "Licenses: $(grep -c '  <-  ' "$DOC/licenses/bundled-libraries.txt") bundled libraries listed, $(ls "$DOC/licenses" | grep -c '\.copyright$') license files"

# Pass 2: pack the AppDir into a single AppImage file.
PATH="$TOOLS:$PATH" "$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --output appimage

echo
echo "Done: $BUILD/$LDAI_OUTPUT"
