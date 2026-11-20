#!/usr/bin/env bash
# S45 (review I1, M5): a program may end while a wait past the helping cap
# (S40) is still pending, with the worker pool running or paused
# (WorkerPool stopProcessing); the runtime runs the turns that wait needs and
# exits. Three cases of the fixture:
#   plain  the pool runs: the shutdown path of the capped wait (S43);
#   pause  the program ends with the pool paused; before the fix a turn made
#          ready while paused released no worker, and the join hung in some
#          runs (12 of 30 on one machine);
#   gate   as pause, and the free worker is held in the pause gate when the
#          program ends; before the fix nothing woke it, and every run hung.
# The timers only shape the order of events; every order must finish, so
# `timeout` is a hang guard, never a pass condition.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
HANG_TIMEOUT="${NESTED_WAIT_TIMEOUT:-120}"
for mode in plain pause gate; do
    out="$(PROTOST_WORKERS=1 timeout "$HANG_TIMEOUT" "$PROTOST" --print-last "$HERE/fixtures/exit_capped_pending.st" "$mode" 2>&1)"
    rc=$?
    [ "$rc" -ne 124 ] || { echo "FAIL ($mode): the program did not exit within ${HANG_TIMEOUT}s"; exit 1; }
    [ "$rc" -eq 0 ] || { echo "FAIL ($mode): exit $rc: $(echo "$out" | tail -n 3)"; exit 1; }
    echo "$out" | grep -qx "main done" || { echo "FAIL ($mode): no answer: $(echo "$out" | tail -n 3)"; exit 1; }
    echo "$out" | grep -qx "last link resumed" || { echo "FAIL ($mode): the pending wait never finished: $(echo "$out" | tail -n 3)"; exit 1; }
done
echo PASS
