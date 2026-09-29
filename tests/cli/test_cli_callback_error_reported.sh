#!/usr/bin/env bash
# An error inside a thenDo: callback is reported on stderr, not dropped, and
# the program continues.
set -uo pipefail
PROTOST="$1"
err="$("$PROTOST" -e "| f | f := Future new. f thenDo: [:v | 1/0]. f resolve: 1. f wait" 2>&1 >/dev/null)"
echo "$err" | grep -q "error in a Future callback: ZeroDivide" || { echo "FAIL: [$err]"; exit 1; }
out="$("$PROTOST" -e "| f | f := Future new. f thenDo: [:v | 1/0]. f resolve: 1. f wait" 2>/dev/null)"
[ "$out" = "1" ] || { echo "FAIL: stdout [$out]"; exit 1; }
echo PASS
