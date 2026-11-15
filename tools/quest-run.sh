#!/bin/bash
# Runs thug2-drive on a Quest over adb (developer mode, USB): the arm64 build (stripped), the game data a Default
# level run reads (tools/default-level-files.txt, from THUG2_TRACE_FILES) and the test park with its rail.
#     tools/quest-run.sh [game folder]      (default ~/Games/THUG2-extract/Game)
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
GAME=${1:-$HOME/Games/THUG2-extract/Game}
ADB=${ADB:-$HOME/Android/platform-tools/adb}
NDK=${ANDROID_NDK_HOME:-$HOME/Android/ndk/26.3.11579264}
STRIP=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
STAGE=$HERE/out/quest-stage
REMOTE=/data/local/tmp/thug2

"$ADB" get-state >/dev/null || { echo "no headset: plug it in, developer mode on, allow USB debugging" >&2; exit 1; }
rm -rf "$STAGE" && mkdir -p "$STAGE"
"$STRIP" -o "$STAGE/thug2-drive" "$HERE/out/android/thug2-drive"
while IFS= read -r file; do
    [ -n "$file" ] || continue
    mkdir -p "$STAGE/game/$(dirname "$file")"
    cp "$GAME/$file" "$STAGE/game/$file"
done < "$HERE/tools/default-level-files.txt"
du -sh "$STAGE"
"$ADB" shell "mkdir -p $REMOTE"
"$ADB" push --sync "$STAGE/." "$REMOTE/" >/dev/null
"$ADB" shell "chmod 755 $REMOTE/thug2-drive && cd $REMOTE && \
    HOME=$REMOTE XDG_CACHE_HOME=$REMOTE/cache THUG2_GAME_DIR=$REMOTE/game THUG2_WORLD=test THUG2_FRAMES=300 \
    THUG2_EVERY=20 THUG2_INPUT='140-400:up,160-164:cross,166-240:triangle' ./thug2-drive -level Default 2>&1 | grep DRIVE"
