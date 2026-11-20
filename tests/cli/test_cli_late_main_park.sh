#!/usr/bin/env bash
# S46 (review I2, M5): the deadlock verdict of a chain of nested waits (S40)
# is reached when the last event of the round is the program thread's own
# park. PROTOST_TEST_HOLD_MAIN_PARK=1 holds the program thread just before it
# marks itself blocked until every worker has confirmed the round, refused it
# (the program thread not yet blocked) and registered for a park: that order,
# which a run otherwise meets only by chance, then happens in every run. The
# hold counts events; it never waits for a time. Before the fix nothing ended
# the round after the park, and the program hung in every run.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
HANG_TIMEOUT="${NESTED_WAIT_TIMEOUT:-120}"
out="$(PROTOST_WORKERS=1 PROTOST_TEST_HOLD_MAIN_PARK=1 timeout "$HANG_TIMEOUT" "$PROTOST" --print-last "$HERE/fixtures/late_main_park.st" 2>&1)"
rc=$?
[ "$rc" -ne 124 ] || { echo "FAIL: no verdict within ${HANG_TIMEOUT}s (the program thread's park was not judged)"; exit 1; }
[ "$rc" -eq 0 ] || { echo "FAIL: exit $rc: $(echo "$out" | tail -n 3)"; exit 1; }
last="$(echo "$out" | tail -n 1)"
[ "$last" = "deadlock: too many nested wait" ] || { echo "FAIL: expected the deadlock error, got: $last"; exit 1; }
echo PASS
