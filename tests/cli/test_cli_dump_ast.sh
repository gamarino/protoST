#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
FIXTURES="$2"
out=$("$PROTOST" --dump-ast "$FIXTURES/hello.st")
grep -q "(module" <<< "$out" || { echo "FAIL: missing (module"; echo "$out"; exit 1; }
grep -q "(cascade" <<< "$out" || { echo "FAIL: missing (cascade"; echo "$out"; exit 1; }
grep -q "(str 'hello')" <<< "$out" || { echo "FAIL: missing string"; echo "$out"; exit 1; }
echo OK
