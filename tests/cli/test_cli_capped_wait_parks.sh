#!/usr/bin/env bash
# S43: a wait nested in a block past the helping cap (S40) parks until an
# event it can act on (its Future settled, a task queued, a change that can
# alter the deadlock verdict); it does not poll. The fixture's capped wait
# lasts 2 s, until a timer settles its Future. A polling wait went through
# about 5,000 idle rounds a second (a 200 us back-off on each of up to 256
# capped workers), which starved the ThreadSanitizer job (CI 44311963261).
# The count of idle rounds that PROTOST_WORKER_STATS=1 reports depends on
# the events, not on the time the wait lasts or the speed of the build: the
# bound is far above what the events give and far below what polling gives.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
HANG_TIMEOUT="${NESTED_WAIT_TIMEOUT:-120}"
out="$(PROTOST_WORKERS=1 PROTOST_WORKER_STATS=1 timeout "$HANG_TIMEOUT" "$PROTOST" --print-last "$HERE/fixtures/capped_wait_timer.st" 2000 2>&1)"
rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL: exit $rc: $(echo "$out" | tail -n 3)"; exit 1; }
echo "$out" | grep -qx "18" || { echo "FAIL: no answer 18: $(echo "$out" | tail -n 3)"; exit 1; }
rounds="$(echo "$out" | sed -n 's/^\[worker-stats\] capped idle rounds \([0-9][0-9]*\)$/\1/p')"
[ -n "$rounds" ] || { echo "FAIL: no capped idle rounds reported: $(echo "$out" | tail -n 3)"; exit 1; }
[ "$rounds" -ge 1 ] || { echo "FAIL: the wait never reached the cap ($rounds idle rounds)"; exit 1; }
[ "$rounds" -le 100 ] || { echo "FAIL: $rounds idle rounds in a 2 s capped wait (it polls)"; exit 1; }
echo PASS
