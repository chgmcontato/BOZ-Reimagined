#!/bin/sh
# Starts the launcher (game data lives in build/game, next to the executable).
# Logs: build/last-run.log (this run, with a per-second performance line) and build/previous-run.log.
cd "$(dirname "$0")" || exit 1
[ -f build/last-run.log ] && mv -f build/last-run.log build/previous-run.log
CODBOZ_PERF=1 exec ./build/codboz-pc "$@" > build/last-run.log 2>&1
