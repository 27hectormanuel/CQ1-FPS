#!/bin/bash
# SPDX-License-Identifier: CC0-1.0
# build-windows.sh [out dir]: builds dinput8.dll for the official Windows Costume Quest (Cq.exe 1.0.0.11
# and the Steam build).
#   MINGW=<i686-w64-mingw32-clang or -gcc>  (default: i686-w64-mingw32-clang on PATH; llvm-mingw works)
#   TEST=1   a test build: reads cq1fps-test.txt (Lua lines, screenshots, a virtual controller)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/out}
MINGW=${MINGW:-i686-w64-mingw32-clang}
mkdir -p "$OUT"
"$MINGW" -O2 -Wall ${TEST:+-DCQ1FPS_TEST} -I"$HERE/src" -shared -o "$OUT/dinput8.dll" \
    "$HERE/src/win/cq1fps_win.c" -luser32 -Wl,--kill-at
echo "Built $OUT/dinput8.dll"
