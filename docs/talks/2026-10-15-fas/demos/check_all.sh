#!/usr/bin/env bash
# Runs every demo RUNS times (default 20) and fails on any deviation.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
RUNS="${RUNS:-20}"
fail=0
for demo in "$HERE"/0*.st; do
    name="$(basename "$demo" .st)"
    ok=0
    for i in $(seq 1 "$RUNS"); do
        if "$HERE/run_demo.sh" "$demo" --quiet; then ok=$((ok+1)); else fail=1; fi
    done
    echo "$name: $ok/$RUNS"
done
exit $fail
