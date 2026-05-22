#!/usr/bin/env bash
#
# D23 regression — un-drained actor mailbox load must never deadlock.
#
# A sender that fires many asynchronous sends at one actor WITHOUT waiting
# on the returned Futures used to wedge the actor scheduler on a fraction
# of runs: a worker blocked acquiring `schedMu` with a plain lock — off any
# GC safepoint while still a counted mutator — stalled the stop-the-world
# GC quorum, so the schedMu holder (parked at a safepoint by design) never
# released it. See docs/STATUS.md D23.
#
# The failure was non-deterministic (~35% of runs hung). A single run is
# therefore a weak guard; this test launches the un-drained workload many
# times and fails fast on the first hang, so a regression is caught with
# very high probability.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SCRIPT="$WORK/undrained.st"
cat > "$SCRIPT" <<'EOF'
Object subclass: #Sink instanceVariableNames: 'count'.
Sink >> initialize  count := 0. ^ self.
Sink >> ping  count := count + 1. ^ count.
Sink >> count  ^ count.
base := Sink new. base initialize.
sink := base asActor.
1 to: 1000 do: [ :i | sink ping ].
(sink count) wait.
EOF

RUNS=25
for i in $(seq 1 "$RUNS"); do
    # A healthy run completes in well under a second; 15s is a generous
    # ceiling that a genuine deadlock will always exceed.
    if ! out="$(timeout 15 "$PROTOST" "$SCRIPT" 2>&1)"; then
        rc=$?
        if [ "$rc" -eq 124 ]; then
            echo "FAIL: run $i/$RUNS DEADLOCKED (timed out) — D23 regression"
        else
            echo "FAIL: run $i/$RUNS exited $rc — output: $out"
        fi
        exit 1
    fi
    last="$(printf '%s\n' "$out" | tail -1)"
    if [ "$last" != "1000" ]; then
        echo "FAIL: run $i/$RUNS produced '$last', expected '1000'"
        exit 1
    fi
done

echo "OK ($RUNS un-drained actor runs, no deadlock)"
