#!/usr/bin/env bash
# Array new: with a size whose list build (protoCore path-copies appends,
# about n * log2(n) cells) cannot fit even in an empty heap signals a
# catchable Error instead of protoCore's out-of-memory exit (reviews R3-2,
# R4-1), and a size that fits is never refused because of garbage or of
# structures dropped since the last collection (reviews R5-1, R6-1). A small
# heap limit makes this cheap: no test allocates more than a few MB. Both
# memory policies, as cli_oom_clean: a fixed limit
# (PROTOCORE_HEAP_LIMIT_CELLS) and the adaptive controller with a small
# budget (PROTOCORE_ADAPTIVE_HEAP=1 makes the same variable its budget). The
# sizes stay clear of the boundary, where the check is an estimate
# (collection_prims.cpp, prim_Array_classNew).
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
  [[ "$out" == "#($ok 'Array new: $big elements need about "*"more than the whole heap can hold (limit $limit cells"*"' $ok)" ]] \
    || { echo "FAIL ($policy): $out"; exit 1; }
done
# Garbage made before the first collection is not in use (review R5-1): a
# size that fits once that garbage is reclaimed must build, not be refused.
# 60,000 dead 9-element arrays leave about 2,000,000 cells handed out and
# no collection yet under a 4,000,000-cell limit; 150,000 elements need about
# 2,700,000 cells and build under round 4's code, which had no check.
prog="| x | 1 to: 60000 do: [ :i | x := Array new: 9 ]. (Array new: 150000) size."
out=$(PROTOCORE_ADAPTIVE_HEAP=0 PROTOCORE_HEAP_LIMIT_CELLS=4000000 timeout 120 "$PROTOST" -e "$prog" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "150000" ]] || { echo "FAIL (garbage before the first collection): rc=$rc"; echo "$out" | tail -3; exit 1; }
# Nor is a structure that was live at the last collection and has been
# dropped since (review R6-1): 100 arrays of 20,000 elements are live while
# 300,000 small arrays force a collection, then dropped; 150,000 elements
# fit once protoCore reclaims them, and round 4's code built it.
prog="| keep x | keep := (1 to: 100) collect: [ :i | Array new: 20000 ].
1 to: 300000 do: [ :i | x := Array new: 9 ].
keep := nil.
[ (Array new: 150000) size ] on: Error do: [ :e | e messageText ]."
out=$(PROTOCORE_ADAPTIVE_HEAP=0 PROTOCORE_HEAP_LIMIT_CELLS=4000000 timeout 120 "$PROTOST" -e "$prog" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "150000" ]] || { echo "FAIL (structure dropped after a collection): rc=$rc"; echo "$out" | tail -3; exit 1; }
# Under a limit below the heap protoCore reserved while bootstrapping, the
# reserved heap is the ceiling: protoCore never grows past it nor shrinks
# below it, and collects within it (review R7-1). Array new: 8000 (about
# 104,000 cells) builds under a 100,000-cell limit and must not be refused;
# a size beyond the reserved heap still is.
prog="| try | try := [ :n | [ (Array new: n) size ] on: Error do: [ :e | e messageText ] ].
{ try value: 8000. try value: 1000000 }."
out=$(PROTOCORE_ADAPTIVE_HEAP=0 PROTOCORE_HEAP_LIMIT_CELLS=100000 timeout 120 "$PROTOST" -e "$prog" 2>&1); rc=$?
[[ $rc -eq 0 && "$out" == "#(8000 'Array new: 1000000 elements need about "*"more than the whole heap can hold (limit 100000 cells, below the "*" cells protoCore has already reserved, which are the ceiling;"*"')" ]] \
  || { echo "FAIL (limit below the reserved heap): rc=$rc"; echo "$out" | tail -3; exit 1; }
echo OK
