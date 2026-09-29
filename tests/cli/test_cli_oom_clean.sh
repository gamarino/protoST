#!/usr/bin/env bash
# Exhausting an explicit heap ceiling ends the program with a clean message and
# exit status 3: no abort, no core dump -- also when it happens in an actor.
set -u
PROTOST="$1"
ulimit -c 0
progs=(
  'a := OrderedCollection new. [true] whileTrue: [a add: (Array new: 100)]'
  'Object subclass: #Hog. Hog >> eat  | a | a := OrderedCollection new. [true] whileTrue: [a add: (Array new: 100)]. ^ a. (Hog new asActor eat) wait'
)
for prog in "${progs[@]}"; do
  out=$(PROTOCORE_HEAP_LIMIT_CELLS=300000 timeout 120 "$PROTOST" -e "$prog" 2>&1); rc=$?
  [[ $rc -eq 3 ]] || { echo "FAIL: rc=$rc for: $prog"; echo "$out" | tail -3; exit 1; }
  [[ "$out" == *"out of memory"* && "$out" != *"terminate called"* && "$out" != *"Aborted"* ]] \
    || { echo "FAIL: message: $out"; exit 1; }
done
echo OK
