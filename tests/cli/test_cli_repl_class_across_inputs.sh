#!/usr/bin/env bash
# REPL: a method entered after its class, in a later input, compiles against
# the class's instance variables, and re-declaring the class keeps its methods.
set -uo pipefail
PROTOST="$1"
out="$(printf '%s\n' \
  "Object subclass: #Foo instanceVariableNames: 'count'." \
  "Foo >> bump  count := (count ifNil: [0]) + 1. ^ count." \
  "Foo >> twice  ^ count * 2." \
  "(Foo new bump; yourself) twice." \
  "Object subclass: #Foo instanceVariableNames: 'count'." \
  "(Foo new bump; bump; yourself) twice." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q "compile error" && { echo "FAIL: $out"; exit 1; }
[ "$(echo "$out" | grep -c '=> 2')" -ge 1 ] || { echo "FAIL (twice): $out"; exit 1; }
echo "$out" | grep -q '=> 4' || { echo "FAIL (redeclared): $out"; exit 1; }
echo PASS
