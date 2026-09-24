#!/usr/bin/env bash
#
# Track S, completed — an actor message payload that only the mailbox
# references must survive the collections that run while it is queued.
#
# This is the test Track S could not write. The mailbox is a protoCore
# `ProtoMPSCQueue` under the actor's `__mailbox__` attribute, and a turn drains
# a whole batch into a C++ local with `takeAll`; `MailboxCursor::adopt` pins
# that batch so the messages the turn has not reached yet stay reachable while
# it runs user code. Nothing could falsify that claim while protoST reclaimed
# nothing (bug S15), so the unit test of Track S asserted only that the messages
# arrived. With S15 fixed, this fixture puts the claim under real collections.
#
# The premise is the heap ceiling. It is set low enough that the workload — far
# more garbage than the ceiling holds — can only finish if cycles run and
# reclaim throughout, and the handlers allocate as well, so collections land
# inside a turn as much as between turns. Each payload is checked by its own
# handler: an array of three copies of the sender's index, none of which is
# referenced by anything but the message, so a payload the collector freed shows
# up as a wrong sum (or a crash), not as a silent pass.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SCRIPT="$WORK/actor_payload_gc.st"
cat > "$SCRIPT" <<'EOF'
Object subclass: #Sink instanceVariableNames: 'sum count bad'.
Sink >> initialize  sum := 0. count := 0. bad := 0. ^ self.
Sink >> take: payload with: expected
  "Verify the payload this message carried: three copies of the sender's
   index. Nothing but the message referenced it between the send and here."
  | ok |
  ok := (payload size = 3)
          and: [ ((payload at: 1) = expected)
          and: [ ((payload at: 2) = expected)
          and: [ (payload at: 3) = expected ] ] ].
  ok ifFalse: [ bad := bad + 1 ].
  "Allocate inside the turn, so collections also land mid-batch."
  1 to: 20 do: [ :i | Array new: 8 ].
  sum := sum + expected.
  count := count + 1.
  ^ sum.
Sink >> total    ^ sum.
Sink >> howMany  ^ count.
Sink >> bad      ^ bad.

sink := (Sink new initialize) asActor.
"Queue the whole backlog before any of it is drained, so the payloads sit in
 the mailbox across many collections rather than being consumed immediately."
WorkerPool stopProcessing.
1 to: 5000 do: [ :i |
    sink take: (Array with: i with: i with: i) with: i ].
WorkerPool startProcessing.
got := (sink total) wait.
howMany := (sink howMany) wait.
bad := (sink bad) wait.
((got = (5000 * 5001 / 2)) and: [ (howMany = 5000) and: [ bad = 0 ] ])
    ifTrue: [ 'PAYLOADS-OK' ]
    ifFalse: [ 'PAYLOADS-BAD sum=' , got printString ,
               ' count=' , howMany printString ,
               ' bad=' , bad printString ].
EOF

LIMIT=500000
RUNS=8

# Repeated, because a lost payload is a race and not every run loses one. The
# defect this fixture caught on its first outing (S16, a mailbox batch that lost
# its only GC root when a turn adopted a second batch) segfaulted about two runs
# in five; eight launches make a miss most unlikely.
for i in $(seq 1 "$RUNS"); do
    rc=0
    out="$(PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" PROTOST_WORKERS=4 \
           timeout 300 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL: run $i/$RUNS exited $rc under a ${LIMIT}-cell ceiling"
        printf '%s\n' "$out"
        exit 1
    fi
    last="$(printf '%s\n' "$out" | tail -n 1)"
    if [ "$last" != "PAYLOADS-OK" ]; then
        echo "FAIL: run $i/$RUNS — $last"
        printf '%s\n' "$out"
        exit 1
    fi
done

# The premise: without reclamation this workload cannot fit in the ceiling. If
# it does fit, the ceiling is no longer forcing collections and the run above
# proved nothing about garbage collection.
rc=0
out="$(PROTOST_NO_GC_SAFEPOINT=1 PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" \
       PROTOST_WORKERS=4 timeout 300 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
if [ "$rc" -eq 0 ]; then
    echo "FAIL: the workload fits in a ${LIMIT}-cell ceiling without reclaiming,"
    echo "      so it does not force the collections this fixture depends on."
    exit 1
fi

echo "OK ($RUNS x 5000 mailbox payloads verified under a ${LIMIT}-cell ceiling)"
