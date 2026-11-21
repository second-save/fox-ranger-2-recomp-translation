#!/usr/bin/env bash
# package_fr2.sh - build Fox Ranger II for macOS, Windows and Linux into dist/.
#
# macOS builds natively; Windows and Linux are cross-compiled with Zig
# (brew install zig) against SDL2 development files you point to:
#   SDL2_MINGW=<SDL2-devel-2.x-mingw>/x86_64-w64-mingw32      (libsdl-org GitHub release)
#   SDL2_LINUX_CMAKE=<dir with an sdl2-config.cmake defining SDL2::SDL2 for x86_64 Linux>
# A platform is skipped when its variable is unset.  No game data is packaged.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DIST="$ROOT/dist"
VER=1.0.0
rm -rf "$DIST"; mkdir -p "$DIST"

docs() {   # $1 = destination folder
    cp README.md "$1/README.md"
    mkdir -p "$1/licenses"
    cp third_party/nuked-opl3/LICENSE "$1/licenses/Nuked-OPL3-LGPL-2.1.txt"
}

if [ "$(uname)" = "Darwin" ]; then
    echo "== macOS"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build --target foxranger2 -j8
    d="$DIST/FoxRanger2-$VER-macOS"; mkdir -p "$d"
    cp -R build/FoxRanger2.app "$d/"
    docs "$d"
    (cd "$DIST" && ditto -c -k --keepParent "FoxRanger2-$VER-macOS" "FoxRanger2-$VER-macOS.zip")
fi

if [ -n "${SDL2_MINGW:-}" ]; then
    echo "== Windows x64"
    cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake -DCMAKE_BUILD_TYPE=Release \
          -DSDL2_DIR="$SDL2_MINGW/lib/cmake/SDL2" >/dev/null
    cmake --build build-win --target foxrt2 -j8
    d="$DIST/FoxRanger2-$VER-windows-x64"; mkdir -p "$d"
    cp build-win/foxrt2.exe "$d/FoxRanger2.exe"
    cp "$SDL2_MINGW/bin/SDL2.dll" "$d/"
    docs "$d"
    cp "$SDL2_MINGW/../LICENSE.txt" "$d/licenses/SDL2-zlib.txt"
    (cd "$DIST" && zip -qr "FoxRanger2-$VER-windows-x64.zip" "FoxRanger2-$VER-windows-x64")
fi

if [ -n "${SDL2_LINUX_CMAKE:-}" ]; then
    echo "== Linux x64"
    cmake -S . -B build-linux -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake -DCMAKE_BUILD_TYPE=Release \
          -DSDL2_DIR="$SDL2_LINUX_CMAKE" >/dev/null
    cmake --build build-linux --target foxrt2 -j8
    d="$DIST/FoxRanger2-$VER-linux-x64"; mkdir -p "$d"
    cp build-linux/foxrt2 "$d/foxranger2"          # stripped at link (cmake/zig-linux.cmake)
    chmod +x "$d/foxranger2"
    cp packaging/FoxRanger2.png "$d/"
    docs "$d"
    printf 'Needs SDL2: sudo apt install libsdl2-2.0-0  (Fedora: sudo dnf install SDL2)\nRun: ./foxranger2\nFolder dialog uses zenity if installed; otherwise drop the game folder on the launcher window.\n' > "$d/LINUX.txt"
    (cd "$DIST" && tar czf "FoxRanger2-$VER-linux-x64.tar.gz" "FoxRanger2-$VER-linux-x64")
fi

ls -la "$DIST"
