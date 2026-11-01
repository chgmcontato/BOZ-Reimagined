#!/usr/bin/env bash
# Rebuild with both features disabled; preserve drops and all other patches.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/../../.."
env -u FRIDA -u TABLET -u LOWEND -u BO1_MUSIC FPS30=0 MATCH_TIMER=0 bash build/build.sh
