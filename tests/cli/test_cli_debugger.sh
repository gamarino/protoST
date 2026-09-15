#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
FIXTURES="$2"

out=$(printf "where\nlocals\nprint 1 + 2\ncont\n" | "$PROTOST" -d "$FIXTURES/halt_demo.st")
grep -q "halted: user halt" <<< "$out" || { echo "FAIL: no halt"; echo "$out"; exit 1; }
grep -q "pc:" <<< "$out"                || { echo "FAIL: no pc"; echo "$out"; exit 1; }
grep -q "  3" <<< "$out"                || { echo "FAIL: print did not evaluate"; echo "$out"; exit 1; }
grep -q "=> 13" <<< "$out"              || { echo "FAIL: final value not 13"; echo "$out"; exit 1; }
echo OK
