#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
FIXTURES="$2"

out=$(printf "where\nlocals\nprint 1 + 2\ncont\n" | "$PROTOST" -d "$FIXTURES/halt_demo.st")
echo "$out" | grep -q "halted: user halt" || { echo "FAIL: no halt"; echo "$out"; exit 1; }
echo "$out" | grep -q "pc:"                || { echo "FAIL: no pc"; echo "$out"; exit 1; }
echo "$out" | grep -q "  3"                || { echo "FAIL: print did not evaluate"; echo "$out"; exit 1; }
echo "$out" | grep -q "=> 13"              || { echo "FAIL: final value not 13"; echo "$out"; exit 1; }
echo OK
