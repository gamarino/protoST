#!/usr/bin/env bash
#
# A recursion that never loops must let collections reclaim its garbage.
#
# protoCore reclaims nothing a context has not submitted (see
# test_cli_gc_reclaims.sh, S15). protoST submits the root context's young
# generation from ExecutionEngine::gcSafepoint, which ran only at a loop
# back-edge and at engine entry. A method send inside the same engine is
# neither, so a program that only recurses -- the textbook fib -- never
# submitted anything: every cell it allocated stayed live, and fib 29 ran out
# of memory under protoST's default 640 MB limit although its live set is a
# few frames deep. Under protoCore's adaptive controller the same gap made
# every cycle futile (fib.st three times slower).
#
# The workload is fib 25 written with sends and inlined ifTrue: only (no
# loop, no block evaluated through a primitive), allocating a small Array per
# call so that the recursion produces garbage of its own. (fib itself
# allocates nothing since an inlined ifTrue: stopped boxing the variables it
# uses into a captured dict; before, that dict was the garbage.) The ceiling
# is about one million cells above the start-up live set, so the run can
# finish only if collections reclaim the recursion's garbage.
#
# Two directions, so the test cannot pass by accident: with
# PROTOST_NO_GC_SAFEPOINT=1 the same run must run out of memory.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SCRIPT="$WORK/fib.st"
cat > "$SCRIPT" <<'ST'
Object subclass: #Fib.
Fib >> compute: n
  | a |
  a := Array new: 4.
  (n <= 1) ifTrue: [ ^ n ].
  ^ (self compute: n - 1) + (self compute: n - 2).
(Fib new compute: 25) printNl.
ST

LIMIT=2700000
EXPECTED=75025

rc=0
out="$(PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" timeout 300 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL: fib 25 did not complete under a ${LIMIT}-cell ceiling (exit $rc)"
    printf '%s\n' "$out" | tail -n 5
    exit 1
fi
last="$(printf '%s\n' "$out" | tail -n 1)"
if [ "$last" != "$EXPECTED" ]; then
    echo "FAIL: produced '$last', expected '$EXPECTED'"
    printf '%s\n' "$out"
    exit 1
fi

rc=0
out="$(PROTOST_NO_GC_SAFEPOINT=1 PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" \
       timeout 300 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
if [ "$rc" -ne 3 ] || ! printf '%s\n' "$out" | grep -q "reclaimed nothing"; then
    echo "FAIL: with the safepoint hook disabled the run did not run out of"
    echo "      memory (exit $rc), so the ceiling no longer bites and this"
    echo "      fixture proves nothing:"
    printf '%s\n' "$out" | tail -n 5
    exit 1
fi

echo "OK (fib 25 under a ${LIMIT}-cell ceiling; out of memory with the hook off)"
