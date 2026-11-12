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
# This script is the end-to-end direction: with the safepoints the run
# completes. The control -- that without the hook an allocation-free loop does
# hold a requested collection -- is the unit case "S3: an allocation-free
# loop does not stall a collection" run with PROTOST_NO_GC_SAFEPOINT=1
# (tests/unit/test_gc_cycle.cpp): it observes that the collector's cycle count
# does not advance while a collection is requested and the loop spins, an
# order of events rather than a timeout. Until fix round 1 of 0.10.0 the
# control was a 10-second run here that had to time out.
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

LIMIT=400000
EXPECTED='#(#churned true)'
run() {   # run <timeout> <script> [env...]
    local t="$1" script="$2"; shift 2
    env "$@" PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" PROTOST_WORKERS=2 \
        timeout "$t" "$PROTOST" --print-last "$script" 2>&1
}

rc=0
out="$(run 300 "$WORK/spin.st")" || rc=$?
last="$(printf '%s\n' "$out" | tail -n 1)"
if [ "$rc" -ne 0 ] || [ "$last" != "$EXPECTED" ]; then
    echo "FAIL: the run did not complete with '$EXPECTED' (exit $rc):"
    printf '%s\n' "$out"
    exit 1
fi

echo "OK (spinning main thread, ${LIMIT}-cell ceiling)"
