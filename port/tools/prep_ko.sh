#!/bin/bash
# Prepare a kernel module for pushing over the serial console:
#   strip -> xz -> report sizes and a rough transfer time at 921600 baud.
#
# Usage: prep_ko.sh <path to .ko> [output dir]
set -u
KO=${1:?usage: prep_ko.sh file.ko [outdir]}
OUT=${2:-/root/ko}
mkdir -p "$OUT"
BASE=$(basename "$KO" .ko)
S="$OUT/$BASE.ko"
X="$OUT/$BASE.ko.xz"

cp -f "$KO" "$S"
aarch64-linux-gnu-strip --strip-debug "$S" 2>/dev/null || true
xz -9 -f -k -c "$S" > "$X"

raw=$(stat -c%s "$S")
comp=$(stat -c%s "$X")
b64=$(( comp * 4 / 3 ))
secs=$(( b64 / 90000 ))   # ~90 KB/s of base64 text at 921600 8N1
echo "module:   $S"
echo "stripped: $raw bytes"
echo "xz:       $comp bytes"
echo "base64:   $b64 chars  (~${secs}s over the console)"
