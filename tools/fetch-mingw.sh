#!/usr/bin/env bash
# Pinned llvm-mingw toolchain for the native cross-build (DLL + EXE).
# One version, one URL, verified by extract + clang/windres sanity checks.
set -euo pipefail

VERSION="20240619"
FLAVOR="llvm-mingw-${VERSION}-msvcrt-ubuntu-20.04-x86_64"
URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${VERSION}/${FLAVOR}.tar.xz"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/.toolchain"
DIR="$DEST/llvm-mingw"

if [ -x "$DIR/bin/x86_64-w64-mingw32-clang" ] && [ -x "$DIR/bin/x86_64-w64-mingw32-windres" ]; then
    echo "==> llvm-mingw $VERSION already at $DIR"
    exit 0
fi

mkdir -p "$DEST"
echo "==> downloading $URL"
curl -fL --retry 3 -o "$DEST/mingw.tar.xz" "$URL"
tar -xJf "$DEST/mingw.tar.xz" -C "$DEST"
mv "$DEST/$FLAVOR" "$DIR"
rm "$DEST/mingw.tar.xz"

"$DIR/bin/x86_64-w64-mingw32-clang" --version | head -1
test -x "$DIR/bin/x86_64-w64-mingw32-windres" || { echo "error: x86_64-w64-mingw32-windres missing in $DIR/bin" >&2; exit 1; }
echo "==> toolchain ready: $DIR (llvm-mingw $VERSION)"
