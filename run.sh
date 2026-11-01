#!/bin/sh
# Starts the launcher (game data lives in build/game, next to the executable).
# Logs: build/last-run.log (this run, with a per-second performance line) and build/previous-run.log.
cd "$(dirname "$(readlink -f "$0")")" || exit 1
EXE=./build/codboz-pc
if [ ! -x "$EXE" ]; then
    echo "run.sh: $EXE not found; build it with: make" >&2
    exit 1
fi
[ -f build/last-run.log ] && mv -f build/last-run.log build/previous-run.log
echo "[run.sh] $EXE" > build/last-run.log
CODBOZ_PERF=1 "$EXE" --data build/game "$@" >> build/last-run.log 2>&1
status=$?
if [ "$status" -ne 0 ]; then
    echo "run.sh: the game exited with code $status. End of build/last-run.log:" >&2
    grep -v 'perf:' build/last-run.log | tail -n 15 >&2
fi
exit "$status"
