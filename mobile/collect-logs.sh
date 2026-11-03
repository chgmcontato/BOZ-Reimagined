#!/usr/bin/env bash
# Collects everything needed to diagnose a BOZ Reimagined Mobile run on the phone.
#
#   bash mobile/collect-logs.sh [adb-serial]
#
# Run it right after reproducing the problem (the app's log file is rewritten on every PLAY).
# Writes boz-mobile-logs-<date>/ and a .tar.gz of it in the current folder.
set -euo pipefail

PKG=com.carlos.boz.mobile
ADB=(adb)
if [[ $# -ge 1 ]]; then ADB=(adb -s "$1"); fi

OUT="boz-mobile-logs-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT"
echo "== device"
{
    echo "model:    $("${ADB[@]}" shell getprop ro.product.model | tr -d '\r')"
    echo "android:  $("${ADB[@]}" shell getprop ro.build.version.release | tr -d '\r') (SDK $("${ADB[@]}" shell getprop ro.build.version.sdk | tr -d '\r'))"
    echo "build:    $("${ADB[@]}" shell getprop ro.build.fingerprint | tr -d '\r')"
    echo "abilist:  $("${ADB[@]}" shell getprop ro.product.cpu.abilist | tr -d '\r')"
    echo "pagesize: $("${ADB[@]}" shell getconf PAGESIZE 2>/dev/null | tr -d '\r')"
    echo "gles:     $("${ADB[@]}" shell getprop ro.opengles.version | tr -d '\r')"
    echo "app:      $("${ADB[@]}" shell dumpsys package "$PKG" | grep -m1 versionName | tr -d '\r ')"
} | tee "$OUT/device.txt"

echo "== logcat (runtime, SDL, crashes)"
"${ADB[@]}" logcat -d -v threadtime -s BozRuntime:V SDL:V SDL/APP:V SDL/ERROR:V DEBUG:V libc:V \
    AndroidRuntime:V ActivityManager:I > "$OUT/logcat-boz.txt" || true
"${ADB[@]}" logcat -d -v threadtime > "$OUT/logcat-full.txt" || true
"${ADB[@]}" logcat -d -b crash -v threadtime > "$OUT/logcat-crash.txt" || true
"${ADB[@]}" shell dumpsys activity exit-info "$PKG" > "$OUT/exit-info.txt" 2>&1 || true

echo "== runtime log file"
FILES="/sdcard/Android/data/$PKG/files"
"${ADB[@]}" pull "$FILES/boz-runtime.log" "$OUT/" 2>/dev/null \
    || echo "(boz-runtime.log not readable over adb on this Android; logcat-boz.txt has the same lines)"
"${ADB[@]}" shell ls -la "$FILES" "$FILES/game" "$FILES/import" > "$OUT/files.txt" 2>&1 || true

echo "== summary"
grep -h -E "memory:|guest address space|dynarmic:|OpenGL|game requested|mode |boz.s3e mapped|error|Fatal|SIGSEGV|SIGABRT|main returned" \
    "$OUT/logcat-boz.txt" "$OUT/logcat-crash.txt" 2>/dev/null | head -60 || true

tar czf "$OUT.tar.gz" "$OUT"
echo "OK -> $OUT.tar.gz"
