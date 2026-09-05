#!/usr/bin/env bash
# Vape V4.22 offline build script.
# Produces GoatClientNative.dll (embeds the Java payload) + GoatClient.exe (auto-injector).
# Requires:
#   - JDK 17 (set JAVA_HOME, or it auto-detects)
#   - llvm-mingw (clang + windres) on PATH, or set MINGW_BIN
# Run from the repo root (same dir as this script). Git Bash on Windows.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE="$ROOT/GoatClient/native"
AUTOINJECT="$NATIVE/autoinject"
OUT="$ROOT/GoatClient/build"

# --- toolchain detection ---
if [ -z "${MINGW_BIN:-}" ]; then
    # llvm-mingw is typically on PATH; try to find it.
    MINGW_BIN="$(dirname "$(command -v clang 2>/dev/null || true)")"
    if [ -z "$MINGW_BIN" ] || [ ! -x "$MINGW_BIN/windres" ]; then
        echo "error: llvm-mingw clang + windres not found on PATH. Set MINGW_BIN." >&2
        exit 1
    fi
fi
CLANG="$MINGW_BIN/clang"
CLANGXX="$MINGW_BIN/clang++"
WINDRES="$MINGW_BIN/windres"

if [ -z "${JAVA_HOME:-}" ]; then
    # prefer a local JDK 17
    for c in "$ROOT/../.jdks/jdk-17"* "$HOME/.jdks/jdk-17"* "$HOME/.jdks/jbr-21"*; do
        if [ -x "$c/bin/java" ]; then JAVA_HOME="$c"; break; fi
    done
fi
if [ -z "${JAVA_HOME:-}" ] || [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "error: JDK 17 not found. Set JAVA_HOME." >&2
    exit 1
fi

echo "==> JAVA_HOME=$JAVA_HOME"
echo "==> MINGW_BIN=$MINGW_BIN"

# --- 1. build the Java payload jar ---
echo "==> [1/4] Building Java injection payload (gradle)"
( cd "$ROOT/GoatClient" && \
  JAVA_HOME="$JAVA_HOME" PATH="$JAVA_HOME/bin:$PATH" \
  ./gradlew clean build verifyInjectionPayload --no-daemon )

JAR="$ROOT/GoatClient/build/libs/goat-client-4.22-injection.jar"
if [ ! -f "$JAR" ]; then
    echo "error: injection jar not found: $JAR" >&2
    exit 1
fi
echo "==> jar: $JAR"

# --- 2. compile native DLL (embeds the jar) ---
echo "==> [2/4] Compiling GoatClientNative.dll"
mkdir -p "$OUT"
WORK="$OUT/native-work"
rm -rf "$WORK"; mkdir -p "$WORK"

JDKINC="$JAVA_HOME/include"
CFLAGS="-O2 -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS -I$JDKINC -I$JDKINC/win32 -I$NATIVE"

"$CLANG" $CFLAGS -c "$NATIVE/dllmain.c"         -o "$WORK/dllmain.o"
"$CLANG" $CFLAGS -c "$NATIVE/loader_bootstrap.c" -o "$WORK/loader_bootstrap.o"
"$CLANG" $CFLAGS -c "$NATIVE/native_bridge.c"    -o "$WORK/native_bridge.o"

# embed the jar as RCDATA resource 421
printf '#include <windows.h>\n421 RCDATA "%s"\n' "$(cygpath -w "$JAR" 2>/dev/null || echo "$JAR")" > "$WORK/payload.rc"
"$WINDRES" "$WORK/payload.rc" "$WORK/payload.o"

"$CLANG" -shared -O2 "$WORK/dllmain.o" "$WORK/loader_bootstrap.o" \
    "$WORK/native_bridge.o" "$WORK/payload.o" -o "$OUT/GoatClientNative.dll" \
    -luser32 -lws2_32
echo "==> $OUT/GoatClientNative.dll"

# --- 3. compile the auto-injector EXE ---
echo "==> [3/4] Compiling GoatClient.exe"
( cd "$AUTOINJECT" && \
  "$WINDRES" app.rc  "$WORK/app.res.o"  && \
  "$WINDRES" version.rc "$WORK/version.res.o" && \
  "$CLANGXX" -O2 -std=c++17 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN \
      -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000000 \
      -municode -c autoinject.cpp -o "$WORK/autoinject.o" && \
  "$CLANGXX" -O2 -municode -mwindows "$WORK/autoinject.o" \
      "$WORK/app.res.o" "$WORK/version.res.o" -o "$OUT/GoatClient.exe" \
      -lgdiplus -lshell32 -lshlwapi -luser32 -lgdi32 -lws2_32 )
echo "==> $OUT/GoatClient.exe"

# --- 4. done ---
echo ""
echo "==> [4/4] Build complete."
echo "    Launcher:  $OUT/GoatClient.exe"
echo "    Native:    $OUT/GoatClientNative.dll"
echo "    Copy both to the same folder and run GoatClient.exe."
