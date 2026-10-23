#!/bin/bash
# SPDX-License-Identifier: CC0-1.0
# build.sh: compiles the Advanced-page additions (advanced.as) into ../src/advanced_action.h. Needs
# Java and JPEXS FFDec (FFDEC=<path to ffdec.jar>). Run it after changing the script.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FFDEC=${FFDEC:?set FFDEC to ffdec.jar}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
python3 "$HERE/make_action.py" "$T/empty.swf" - --empty
mkdir -p "$T/src/scripts/frame_1"
cp "$HERE/advanced.as" "$T/src/scripts/frame_1/DoAction.as"
java -jar "$FFDEC" -importScript "$T/empty.swf" "$T/out.swf" "$T/src" >/dev/null
python3 "$HERE/make_action.py" "$T/out.swf" "$HERE/../src/advanced_action.h" kAdvancedAction
