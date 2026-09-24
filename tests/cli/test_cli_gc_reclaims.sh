#!/usr/bin/env bash
#
# S15 regression — a collection cycle must actually reclaim the garbage a
# protoST program produces.
#
# protoCore reclaims nothing a context has not submitted: every cell a context
# allocates is chained onto that context's young generation, and the root scan
# marks that whole chain as live. A context submits its chain when it is
# destroyed, or from `ProtoContext::safepoint()` past a per-context threshold.
# protoST runs a whole program on the runtime's root context and creates no
# context of its own, so until the interpreter gained a safepoint at the loop
# back-edge and at engine entry, the root context's chain was never submitted:
# every cell the program ever allocated stayed permanently live, every cycle
# reclaimed zero, and the heap only ever grew.
#
# The premise of this fixture is that the workload allocates far more than the
# ceiling allows, so it can only finish if collections reclaim: 100,000 arrays
# of 8 elements against a hard ceiling of 400,000 cells and a live set of about
# 180,000. The protoCore heap-limit path aborts with "last cycle reclaimed 0 —
# out of memory" when nothing can be reclaimed, which is exactly the S15
# symptom.
#
# It is a two-directional test, so it cannot pass by accident: the same
# workload is then run with `PROTOST_NO_GC_SAFEPOINT=1`, which disables the
# interpreter's safepoint hook, and that run MUST abort. If it does not, either
# the hook is no longer what makes the difference or the ceiling stopped
# biting, and this fixture is no longer testing what it claims.
set -euo pipefail
PROTOST="$1"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
SCRIPT="$WORK/gc_reclaims.st"
cat > "$SCRIPT" <<'EOF'
kept := OrderedCollection new.
1 to: 100000 do: [ :i |
    | a |
    a := Array new: 8.
    a at: 1 put: i.
    "Keep a bounded live set so the ceiling is hit by garbage, not by
     survivors: one array in a hundred stays reachable, and only the last
     thousand of those."
    (i \\ 100) = 0 ifTrue: [
        kept add: a.
        kept size > 1000 ifTrue: [ kept removeFirst ] ] ].
kept size.
EOF

LIMIT=400000
EXPECTED=1000

# --- direction 1: with the safepoint hook, the run completes ----------------
rc=0
out="$(PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" PROTOST_WORKERS=2 \
       timeout 300 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
if [ "$rc" -ne 0 ]; then
    echo "FAIL: the run did not complete under a ${LIMIT}-cell ceiling (exit $rc)"
    printf '%s\n' "$out"
    exit 1
fi
last="$(printf '%s\n' "$out" | tail -n 1)"
if [ "$last" != "$EXPECTED" ]; then
    echo "FAIL: produced '$last', expected '$EXPECTED'"
    printf '%s\n' "$out"
    exit 1
fi

# --- direction 2: without it, the same run must run out of memory -----------
rc=0
out="$(PROTOST_NO_GC_SAFEPOINT=1 PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" \
       PROTOST_WORKERS=2 timeout 300 "$PROTOST" "$SCRIPT" 2>&1)" || rc=$?
if [ "$rc" -eq 0 ]; then
    echo "FAIL: the run completed with the safepoint hook DISABLED, so this"
    echo "      fixture no longer proves the hook is what reclaims the heap."
    printf '%s\n' "$out"
    exit 1
fi
if ! printf '%s\n' "$out" | grep -q "reclaimed 0"; then
    echo "FAIL: with the hook disabled the run failed for some other reason"
    echo "      than a cycle that reclaimed nothing (exit $rc):"
    printf '%s\n' "$out"
    exit 1
fi

echo "OK (100000 arrays under a ${LIMIT}-cell ceiling; aborts with the hook off)"
