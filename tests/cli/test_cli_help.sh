#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
"$PROTOST" --help 2>&1 | grep -q "Usage:" || { echo "FAIL: --help missing 'Usage:'"; exit 1; }
"$PROTOST" --version  | grep -qE "^protoST 0\.[0-9]+\." || { echo "FAIL: --version output"; exit 1; }
"$PROTOST" no-such-option && { echo "FAIL: expected non-zero for unknown option"; exit 1; } || true
echo OK
