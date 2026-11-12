#!/usr/bin/env bash
#
# S3 regression -- a thread running an allocation-free loop must not stall a
# garbage collection.
#
# protoCore starts a requested collection only after every running thread
# parks. A thread that never allocates never reaches allocCell's poll, so
# without a park point in the dispatch loop it holds every other thread in
# the stop-the-world handshake for as long as it loops -- for ever when it
# waits on a flag that a parked thread must set. The interpreter parks at
# every loop back-edge (ExecutionEngine::gcSafepoint, ProtoContext::safepoint).
#
# The fixture is exactly that deadlock shape. The main thread spins on
# `[ flag value ] whileFalse: [ ... ]`, which allocates nothing (an Atom read
# and a SmallInteger increment); an actor churns through far more garbage than
# the heap ceiling holds, so it can only finish after collections ran while
# the main thread was spinning, and then sets the flag.
#
# Three directions, so it cannot pass by accident:
#   1. with the safepoints, the run completes;
#   2. with PROTOST_NO_GC_SAFEPOINT=1 it must NOT complete: the first
#      collection the actor needs waits for the spinning thread for ever
#      (the bounded `timeout` only ends a run that is known to never end);
#   3. the same program without the spinning loop, also with the hook off,
#      ends -- with the out-of-memory the disabled hook causes (S15) -- so the
#      hang of direction 2 is the spinning loop, not something else.
# Until 0.10.0 this test could not be written: every protoST loop allocated
# (a method temporary used in an inlined block was captured, and each write
# published a new version of the captured dictionary), so every loop parked at
# allocCell's poll anyway.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cat > "$WORK/spin.st" <<'EOS'
Object subclass: #Churner.
Churner >> churn: flag
  1 to: 300000 do: [ :i | Array new: 8 ].
  flag value: true ifCurrent: false.
  ^ #churned
| flag f spins |
flag := Atom on: false.
f := Churner new asActor churn: flag.
spins := 0.
[ flag value ] whileFalse: [ spins := spins + 1 ].
{ f wait. spins > 0 }
EOS
sed 's/^\[ flag value \] whileFalse: \[ spins := spins + 1 \]\./spins := 1./' \
    "$WORK/spin.st" > "$WORK/nospin.st"
if grep -q whileFalse "$WORK/nospin.st"; then
    echo "FAIL: the control program still contains the spinning loop"; exit 1
fi

LIMIT=400000
EXPECTED='#(#churned true)'
run() {   # run <timeout> <script> [env...]
    local t="$1" script="$2"; shift 2
    env "$@" PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" PROTOST_WORKERS=2 \
        timeout "$t" "$PROTOST" --print-last "$script" 2>&1
}

# --- direction 1: with the safepoints, the run completes --------------------
rc=0
out="$(run 300 "$WORK/spin.st")" || rc=$?
last="$(printf '%s\n' "$out" | tail -n 1)"
if [ "$rc" -ne 0 ] || [ "$last" != "$EXPECTED" ]; then
    echo "FAIL: the run did not complete with '$EXPECTED' (exit $rc):"
    printf '%s\n' "$out"
    exit 1
fi

# --- direction 2: without them, it never completes --------------------------
rc=0
out="$(run 10 "$WORK/spin.st" PROTOST_NO_GC_SAFEPOINT=1)" || rc=$?
if [ "$rc" -ne 124 ]; then
    echo "FAIL: with the safepoint hook DISABLED the run ended (exit $rc), so"
    echo "      this fixture no longer shows that the back-edge parks:"
    printf '%s\n' "$out"
    exit 1
fi

# --- direction 3: without the spinning loop, the hook-off run ends ----------
rc=0
out="$(run 300 "$WORK/nospin.st" PROTOST_NO_GC_SAFEPOINT=1)" || rc=$?
if [ "$rc" -eq 124 ]; then
    echo "FAIL: the control program hangs too, so direction 2's hang is not"
    echo "      the spinning loop's"
    exit 1
fi

echo "OK (spinning main thread, ${LIMIT}-cell ceiling; hangs with the hook off, control ends with exit $rc)"
