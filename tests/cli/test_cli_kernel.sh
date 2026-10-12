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
# A missing kernel stops startup too, with a message that names it and a
# nonzero exit, in every mode that runs code: before 0.6.3 protost ran with
# the primitives alone and failed later on an undefined Transcript.
mkdir -p "$SCRATCH/nokernel"
printf "Transcript showCr: 'SCRIPT-RAN'.\n" > "$SCRATCH/nokernel/script.st"
for mode in "-e 1" "$SCRATCH/nokernel/script.st"; do
  # shellcheck disable=SC2086
  out=$(PROTOST_LIB="$SCRATCH/nokernel" "$PROTOST" $mode 2>&1 < /dev/null); rc=$?
  [[ $rc -ne 0 && "$out" == *"cannot find the protoST kernel"* && "$out" == *"PROTOST_LIB"* \
     && "$out" != *"SCRIPT-RAN"* ]] \
    || { echo "FAIL: missing kernel not reported for '$mode': rc=$rc out=$out"; exit 1; }
done
# Nanoseconds since the epoch. BSD date (macOS) has no %N: it prints a
# literal N, and perl takes over there.
now_ns() {
  local t; t=$(date +%s%N)
  if [[ "$t" =~ ^[0-9]+$ ]]; then echo "$t"
  else perl -MTime::HiRes=time -e 'printf "%.0f\n", time() * 1e9'; fi
}
# What is measured is what loading the kernel and evaluating `1` adds to
# starting the process: the best of five `-e '1'` runs minus the best of five
# `--version` runs, which start the same executable and load no kernel. The
# launch is taken off on every platform, so the budget means the same
# everywhere: under Git for Windows' bash, starting any native program costs
# about as much as the whole budget (about 95 ms, against 45 ms from cmd.exe),
# and on Linux and macOS it is a few milliseconds.
best_of_five() {
  local best=999999 start end ms
  for i in 1 2 3 4 5; do
    start=$(now_ns); "$PROTOST" "$@" >/dev/null; end=$(now_ns)
    ms=$(( (end-start)/1000000 )); (( ms < best )) && best=$ms
  done
  echo "$best"
}
run=$(best_of_five -e '1')
launch=$(best_of_five --version)
best=$(( run - launch ))
[[ $best -lt 100 ]] || { echo "FAIL: startup ${best} ms >= 100 (-e '1' ${run} ms, launch ${launch} ms)"; exit 1; }
echo "OK (startup ${best} ms: -e '1' ${run} ms, launch ${launch} ms)"
