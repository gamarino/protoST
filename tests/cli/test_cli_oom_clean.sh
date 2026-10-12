#!/usr/bin/env bash
# Exhausting the heap ends the program with a clean message and exit status 3:
# no abort, no core dump -- also when it happens in an actor, and under both
# memory policies: a fixed limit (PROTOCORE_HEAP_LIMIT_CELLS) and the adaptive
# controller (the default since 0.6.2; here given a small budget with
# PROTOCORE_ADAPTIVE_HEAP=1, which makes PROTOCORE_HEAP_LIMIT_CELLS its budget).
set -u
PROTOST="$1"
ulimit -c 0
progs=(
  'a := OrderedCollection new. [true] whileTrue: [a add: (Array new: 100)]'
  'Object subclass: #Hog. Hog >> eat  | a | a := OrderedCollection new. [true] whileTrue: [a add: (Array new: 100)]. ^ a. (Hog new asActor eat) wait'
)
for policy in fixed adaptive; do
  if [[ $policy == fixed ]]; then mode=0; limit=300000; else mode=1; limit=1500000; fi
  for prog in "${progs[@]}"; do
    out=$(PROTOCORE_ADAPTIVE_HEAP=$mode PROTOCORE_HEAP_LIMIT_CELLS=$limit timeout 120 "$PROTOST" -e "$prog" 2>&1); rc=$?
    [[ $rc -eq 3 ]] || { echo "FAIL ($policy): rc=$rc for: $prog"; echo "$out" | tail -3; exit 1; }
    [[ "$out" == *"out of memory"* && "$out" == *"heap limit of $limit cells"* \
       && "$out" != *"terminate called"* && "$out" != *"Aborted"* ]] \
      || { echo "FAIL ($policy): message: $out"; exit 1; }
  done
done
echo OK
