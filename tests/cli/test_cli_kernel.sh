#!/usr/bin/env bash
# The Smalltalk-source kernel (lib/kernel) is loaded at start: a broken kernel
# file stops startup naming the file and line, and startup stays fast.
set -u
PROTOST="$1"; SCRATCH="$2"
rm -rf "$SCRATCH" && mkdir -p "$SCRATCH/kernel"
printf 'object.st\n' > "$SCRATCH/kernel/00-manifest.txt"
printf 'Object >> broken\n  ^ ( .\n' > "$SCRATCH/kernel/object.st"
out=$(PROTOST_LIB="$SCRATCH" "$PROTOST" -e '1' 2>&1); rc=$?
[[ $rc -ne 0 && "$out" == *"object.st:2"* ]] || { echo "FAIL: broken kernel not reported: rc=$rc out=$out"; exit 1; }
best=999999
for i in 1 2 3 4 5; do
  start=$(date +%s%N); "$PROTOST" -e '1' >/dev/null; end=$(date +%s%N)
  ms=$(( (end-start)/1000000 )); (( ms < best )) && best=$ms
done
[[ $best -lt 100 ]] || { echo "FAIL: startup ${best} ms >= 100"; exit 1; }
echo "OK (startup ${best} ms)"
