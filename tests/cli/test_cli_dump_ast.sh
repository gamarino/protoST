#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
FIXTURES="$2"
out=$("$PROTOST" --dump-ast "$FIXTURES/hello.st")
echo "$out" | grep -q "(module" || { echo "FAIL: missing (module"; echo "$out"; exit 1; }
echo "$out" | grep -q "(cascade" || { echo "FAIL: missing (cascade"; echo "$out"; exit 1; }
echo "$out" | grep -q "(str 'hello')" || { echo "FAIL: missing string"; echo "$out"; exit 1; }
echo OK
