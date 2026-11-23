#!/usr/bin/env bash
# S48: a wait that helps (a wait nested in a block) regains control after
# every message it runs, so an actor that keeps sending itself messages
# cannot bury it, timers keep settling, and the helped actor keeps its order
# and its single runner. Each fixture runs with one worker (the interleaving
# is then deterministic: every helped message is a turn of its own) and with
# the default pool. The first is also a conformance program
# (tests/conformance/10-actors/helping-wait-under-self-sending-actor.st),
# which the conformance suite runs with the default pool only. Each run is bounded; before 0.16.1 the first two hung
# with one worker.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
BOUND="${HELPING_TIMEOUT:-120}"
fail=0

check() {  # check <label> <workers or ""> <fixture> <expected output>
    local label="$1" workers="$2" fixture="$3" expected="$4" out rc
    if [ -n "$workers" ]; then
        out="$(PROTOST_WORKERS="$workers" timeout "$BOUND" "$PROTOST" "$HERE/fixtures/$fixture" 2>&1)"
    else
        out="$(timeout "$BOUND" "$PROTOST" "$HERE/fixtures/$fixture" 2>&1)"
    fi
    rc=$?
    if [ "$rc" -ne 0 ]; then
        [ "$rc" -eq 124 ] && out="(a hang, killed after ${BOUND} s) $out"
        echo "FAIL: $label: exit $rc: $out"; fail=1; return
    fi
    if [ "$out" != "$expected" ]; then
        echo "FAIL: $label: got:"; echo "$out"; echo "expected:"; echo "$expected"; fail=1; return
    fi
    echo "ok: $label"
}

for w in 1 ""; do
    pool="${w:+workers=$w}"; pool="${pool:-default pool}"
    check "self-sender, $pool" "$w" ../../conformance/10-actors/helping-wait-under-self-sending-actor.st \
"spins before the stop: true
#resumed"
    check "waiting self-sender, $pool" "$w" helping_self_sender_waits.st \
"spins before the stop: true
#resumed"
    check "timers under a self-sender, $pool" "$w" helping_self_sender_timers.st \
"#('timer' 'timedOut')"
    check "mailbox order, $pool" "$w" helping_mailbox_order.st \
"#resumed
overlap: false
order kept: true"
done

[ "$fail" -eq 0 ] && echo PASS || exit 1
