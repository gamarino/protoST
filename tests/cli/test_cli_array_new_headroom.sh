#!/usr/bin/env bash
# Array new: with a size below the heap ceiling but beyond what the heap can
# still take (protoCore builds the list by path-copying appends, about
# n * log2(n) cells) signals a catchable Error instead of protoCore's
# out-of-memory exit (review R4-1). A small heap limit makes this cheap: no
# test allocates more than a few MB. Both memory policies, as cli_oom_clean:
# a fixed limit (PROTOCORE_HEAP_LIMIT_CELLS) and the adaptive controller
# with a small budget (PROTOCORE_ADAPTIVE_HEAP=1 makes the same variable its
# budget). The sizes stay clear of the boundary, where the check is an
# estimate (collection_prims.cpp, prim_Array_classNew).
set -u
PROTOST="$1"
ulimit -c 0
for policy in fixed adaptive; do
  if [[ $policy == fixed ]]; then mode=0; limit=400000; ok=10000; big=100000
  else mode=1; limit=1500000; ok=20000; big=1000000; fi
  prog="| try | try := [ :n | [ (Array new: n) size ] on: Error do: [ :e | e messageText ] ].
{ try value: $ok. try value: $big. try value: $ok } printNl."
  out=$(PROTOCORE_ADAPTIVE_HEAP=$mode PROTOCORE_HEAP_LIMIT_CELLS=$limit timeout 120 "$PROTOST" -e "$prog" 2>&1); rc=$?
  [[ $rc -eq 0 ]] || { echo "FAIL ($policy): rc=$rc"; echo "$out" | tail -3; exit 1; }
  [[ "$out" == "#($ok 'Array new: $big elements need about "*"more than the heap can still hold (limit $limit cells"*"' $ok)" ]] \
    || { echo "FAIL ($policy): $out"; exit 1; }
done
echo OK
