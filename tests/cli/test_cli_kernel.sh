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
# Nanoseconds since the epoch. BSD date (macOS) has no %N: it prints a
# literal N, and perl takes over there.
now_ns() {
  local t; t=$(date +%s%N)
  if [[ "$t" =~ ^[0-9]+$ ]]; then echo "$t"
  else perl -MTime::HiRes=time -e 'printf "%.0f\n", time() * 1e9'; fi
}
best=999999
for i in 1 2 3 4 5; do
  start=$(now_ns); "$PROTOST" -e '1' >/dev/null; end=$(now_ns)
  ms=$(( (end-start)/1000000 )); (( ms < best )) && best=$ms
done
# Under Git for Windows' bash, starting any native program costs about as much
# as the whole budget (about 95 ms against 45 ms from cmd.exe); there the
# launch itself, measured with --version, which loads no kernel, is taken off.
if command -v cygpath >/dev/null 2>&1; then
  launch=999999
  for i in 1 2 3 4 5; do
    start=$(now_ns); "$PROTOST" --version >/dev/null; end=$(now_ns)
    ms=$(( (end-start)/1000000 )); (( ms < launch )) && launch=$ms
  done
  best=$(( best - launch ))
fi
[[ $best -lt 100 ]] || { echo "FAIL: startup ${best} ms >= 100"; exit 1; }
echo "OK (startup ${best} ms)"
