#!/usr/bin/env bash
#
# D25 regression: concurrent first execution of a method must not corrupt the
# heap.
#
# BytecodeModule used to fill three per-module caches lazily and without
# synchronisation: the interned selector/name symbols (constSym), the mangled
# instance-variable keys (ivSymbol) and the parsed call-form descriptors
# (callDescriptorSlot). Each was a std::vector that was (re)allocated on first
# use. When two worker threads ran the same method for the first time at the
# same moment, both could re-allocate the vector; one freed the buffer the
# other was still writing into, which corrupted glibc's tcache
# ("malloc(): unaligned tcache chunk detected",
# "tcache_thread_shutdown(): unaligned tcache chunk detected") or made an
# instance variable read as nil. See docs/STATUS.md D25.
#
# The generated program defines METHODS fresh methods and, for each one, sends
# it to 8 actors back to back, so 8 workers execute a never-run module at the
# same time. Every method exercises all three caches: a keyword send
# (constSym), instance-variable reads (ivSymbol) and a call-form send
# (call descriptor). Pre-fix roughly one launch in ten failed, so the program is
# launched RUNS times and the test fails on the first crash, hang or wrong
# result.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SCRIPT="$WORK/first_call.st"

METHODS=200
RUNS=40
ACTORS=8
EXPECTED=$((METHODS * ACTORS * 3))

{
    echo "Object subclass: #W instanceVariableNames: 'a b'."
    echo "W >> init  a := 1. b := 2. ^ self."
    echo "W >> id: x  ^ x."
    echo "W >> cf(x)  ^ x."
    for m in $(seq 1 "$METHODS"); do
        echo "W >> m$m  ^ (self id: a) + (self cf(b))."
    done
    echo "ws := OrderedCollection new."
    echo "1 to: $ACTORS do: [ :i | ws add: W new init asActor ]."
    echo "sum := 0."
    for m in $(seq 1 "$METHODS"); do
        echo "futs := OrderedCollection new. ws do: [ :w | futs add: w m$m ]. futs do: [ :f | sum := sum + f wait ]."
    done
    echo "sum."
} > "$SCRIPT"

for i in $(seq 1 "$RUNS"); do
    # A healthy launch takes well under a second; 30 s only catches a hang.
    set +e
    out="$(PROTOST_WORKERS=$ACTORS timeout 30 "$PROTOST" "$SCRIPT" 2>&1)"
    rc=$?
    set -e
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -eq 124 ]; then
            echo "FAIL: run $i/$RUNS timed out (D25 regression)"
        else
            echo "FAIL: run $i/$RUNS exited $rc (D25 regression) — output: $out"
        fi
        exit 1
    fi
    last="$(printf '%s\n' "$out" | tail -1)"
    if [ "$last" != "$EXPECTED" ]; then
        echo "FAIL: run $i/$RUNS produced '$last', expected '$EXPECTED' (D25 regression)"
        exit 1
    fi
done

echo "OK ($RUNS launches, $METHODS methods first run on $ACTORS workers at once)"
