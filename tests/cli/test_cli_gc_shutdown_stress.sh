#!/usr/bin/env bash
#
# S4 regression — blocking waits and the shutdown join while actors allocate
# must neither hang nor corrupt memory.
#
# Eight actors allocate garbage. The main thread waits on their futures from
# inside a collection iteration (a wait nested under a primitive), fires a
# second round of sends without waiting, and ends while the actors are still
# busy, so the runtime joins its workers while they allocate. Every blocked
# thread must leave the stop-the-world quorum exactly once: before S4 idle
# workers and waiters lowered `runningThreads` while unmanaged regions raised
# `parkedThreads`, and the two could count one thread twice.
#
# The test runs with the default protoCore configuration, so a collection runs
# only when protoCore's own pacing starts one; it does not force collections.
#
# A failure is non-deterministic, so the workload is launched repeatedly and
# the test fails on the first non-zero exit, timeout or wrong result.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SCRIPT="$WORK/shutdown_churn.st"
cat > "$SCRIPT" <<'EOF'
Object subclass: #Churn instanceVariableNames: 'id'.
Churn >> setId: n  id := n. ^ self.
Churn >> churn: rounds
  | a |
  1 to: rounds do: [ :i | a := Array new: 100 ].
  ^ id.
workers := OrderedCollection new.
1 to: 8 do: [ :i | workers add: ((Churn new setId: i) asActor) ].
pending := Set new.
workers do: [ :w | pending add: (w churn: 400) ].
sum := 0.
pending do: [ :f | sum := sum + f wait ].
workers do: [ :w | w churn: 400 ].
sum.
EOF

RUNS=20
for i in $(seq 1 "$RUNS"); do
    rc=0
    out="$(PROTOST_WORKERS=8 timeout 20 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
    if [ "$rc" -ne 0 ]; then
        if [ "$rc" -eq 124 ]; then
            echo "FAIL: run $i/$RUNS timed out"
        else
            echo "FAIL: run $i/$RUNS exited $rc — output: $out"
        fi
        exit 1
    fi
    last="$(printf '%s\n' "$out" | tail -n 1)"
    if [ "$last" != "36" ]; then
        echo "FAIL: run $i/$RUNS produced '$last', expected '36'"
        exit 1
    fi
done

echo "OK ($RUNS launches: waits and shutdown while actors allocate)"
