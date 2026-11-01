#!/bin/sh
# Starts the launcher (game data lives in build/game, next to the executable).
# Uses the Dynarmic build (build-dynarmic/, from `make EMU=dynarmic`) when it exists;
# CODBOZ_EMU=unicorn forces the Unicorn build. Both share the same data in build/game.
# Logs: build/last-run.log (this run, with a per-second performance line) and build/previous-run.log.
cd "$(dirname "$0")" || exit 1
[ -f build/last-run.log ] && mv -f build/last-run.log build/previous-run.log
EXE=./build/codboz-pc
if [ "${CODBOZ_EMU:-}" != unicorn ] && [ -x ./build-dynarmic/codboz-pc ]; then
    EXE=./build-dynarmic/codboz-pc
fi
echo "[run.sh] $EXE" > build/last-run.log
CODBOZ_PERF=1 exec "$EXE" --data build/game "$@" >> build/last-run.log 2>&1
