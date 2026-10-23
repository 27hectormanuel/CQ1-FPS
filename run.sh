#!/bin/bash
# SPDX-License-Identifier: CC0-1.0
# run.sh <game dir> [game options]: starts the official Linux Costume Quest (the folder holding
# Cq.bin.x86) with CQ1-FPS (cq1fps.so, next to this script). The Steam runtime's 32-bit libraries
# are used when Steam is installed (the game needs libGLU, which few systems ship for i386).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
GAME=$(cd "${1:?usage: run.sh <game dir> [options]}" && pwd); shift
RT=${STEAM_RUNTIME_DIR:-}
for d in "$HOME/.steam/steam/steamapps/common/SteamLinuxRuntime/steam-runtime" "$HOME/.steam/steam/ubuntu12_32/steam-runtime"; do
    [ -z "$RT" ] && [ -d "$d/usr/lib/i386-linux-gnu" ] && RT=$d
done
RT=${RT:-/nonexistent}
cd "$GAME"
export LD_LIBRARY_PATH="$GAME/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Preloaded into the (32-bit) game only, not the runtime's 64-bit helpers.
PRE="LD_PRELOAD=$HERE/cq1fps.so${LD_PRELOAD:+:$LD_PRELOAD}"
if [ -x "$RT/run.sh" ]; then exec "$RT/run.sh" env "$PRE" ./Cq.bin.x86 "$@"; fi
exec env "$PRE" ./Cq.bin.x86 "$@"
