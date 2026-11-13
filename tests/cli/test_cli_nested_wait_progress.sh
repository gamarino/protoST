#!/usr/bin/env bash
# S40: a wait nested in a block blocks its worker, which runs other actors'
# messages meanwhile, up to 16 nested on one thread. Past that cap the program
# must still finish: a chain of 200 actors each waiting nested on the next
# (one worker), and 200 clients each waiting nested on one server (one worker,
# then the default pool). The answers are counts; nothing depends on timing.
# Past the cap the pool adds a worker; it grows to 256 workers, 17 buried
# actors each, so a chain of 5000 cannot finish: it must end with the
# `deadlock: too many nested waits` Error, not hang (the main thread is parked
# in its wait, no timer is pending: nothing else can release a wait). `timeout` only turns a
# hang (the defect) into a failure.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
fail=0
run() {   # run <expected> <workers or empty> <fixture> <arg>
    local expected="$1" workers="$2" fixture="$3" arg="$4" out rc
    if [ -n "$workers" ]; then
        out="$(PROTOST_WORKERS="$workers" timeout 120 "$PROTOST" --print-last "$HERE/fixtures/$fixture" "$arg" 2>&1)"
    else
        out="$(timeout 120 "$PROTOST" --print-last "$HERE/fixtures/$fixture" "$arg" 2>&1)"
    fi
    rc=$?
    if [ "$rc" -ne 0 ] || [ "$(echo "$out" | tail -n 1)" != "$expected" ]; then
        echo "FAIL: $fixture $arg (workers ${workers:-default}): exit $rc: $(echo "$out" | tail -n 3)"
        fail=1
    fi
}
run 200 1 nested_wait_chain.st 200
run 2000 1 nested_wait_chain.st 2000
run 200 1 nested_wait_clients.st 200
run 200 "" nested_wait_clients.st 200
run 2000 "" nested_wait_clients.st 2000
# Review I-3: a capped worker runs a timer callback that waits (the thread
# counts once as capped).
run 24 1 nested_wait_capped_callback.st 20
run 44 "" nested_wait_capped_callback.st 40
# Review I-1: worker 0's top wait is on a timer while a chain fills the other
# 255 workers and needs one more turn, which worker 0 runs once the timer
# fires. No deadlock may be signalled, whether the timer fires before the pool
# fills (1 ms) or long after (3000 ms); the answer is 17 + 4336.
run4() {  # like run, with two arguments
    local expected="$1" fixture="$2" out rc
    out="$(PROTOST_WORKERS=1 timeout 120 "$PROTOST" --print-last "$HERE/fixtures/$fixture" "$3" "$4" 2>&1)"
    rc=$?
    if [ "$rc" -ne 0 ] || [ "$(echo "$out" | tail -n 1)" != "$expected" ]; then
        echo "FAIL: $fixture $3 $4 (one worker): exit $rc: $(echo "$out" | tail -n 3)"
        fail=1
    fi
}
run4 4353 nested_wait_timer_release.st 4336 3000
run4 4353 nested_wait_timer_release.st 4336 1
out="$(PROTOST_WORKERS=1 timeout 120 "$PROTOST" --print-last "$HERE/fixtures/nested_wait_chain.st" 5000 2>&1)"
rc=$?
if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || ! echo "$out" | grep -q "deadlock: too many nested waits"; then
    echo "FAIL: chain of 5000 (one worker): exit $rc: $(echo "$out" | tail -n 3)"
    fail=1
fi
[ "$fail" -eq 0 ] && echo PASS
exit "$fail"
