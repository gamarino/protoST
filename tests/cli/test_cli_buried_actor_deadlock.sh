#!/usr/bin/env bash
# A wait on an actor buried under the waiter on the same worker thread is
# reported as a deadlock (one worker makes the interleaving deterministic).
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
out="$(PROTOST_WORKERS=1 timeout 20 "$PROTOST" --print-last "$HERE/fixtures/buried_actor.st" 2>&1)"
rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL: exit $rc: $out"; exit 1; }
[ "$(echo "$out" | tail -1)" = "deadlock" ] || { echo "FAIL: $out"; exit 1; }
echo PASS
