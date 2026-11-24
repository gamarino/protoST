#!/usr/bin/env bash
# Explicit-state spec §6.9: the REPL's top-level assignment (STORE_GLOBAL)
# cannot rebind the reserved kernel class UnsetConstantError. The assignment
# signals the reserved-name Error (the global cannot be rebound), the global stays the kernel class, and a
# read of an unset constant is still caught by `on: UnsetConstantError do:`.
set -uo pipefail
PROTOST="$1"
out="$(printf '%s\n' \
  "kernel := UnsetConstantError." \
  "UnsetConstantError := 42." \
  "(UnsetConstantError == kernel) printNl." \
  "Object subclass: #Account instanceVariableNames: '' constantNames: 'owner'." \
  "Account >> owner  ^ owner." \
  "([Account basicNew owner] on: UnsetConstantError do: [:e | 'caught']) printNl." | "$PROTOST" -i 2>&1)"
echo "$out" | grep -q 'UnsetConstantError is a reserved kernel class name; the global cannot be rebound' \
  || { echo "FAIL (no refusal): $out"; exit 1; }
echo "$out" | grep -q '^=> true$' || { echo "FAIL (rebound): $out"; exit 1; }
echo "$out" | grep -q '^=> caught$' || { echo "FAIL (not caught): $out"; exit 1; }
echo PASS
