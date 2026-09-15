#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
"$PROTOST" --help 2>&1 | grep -q "Usage:" || { echo "FAIL: --help missing 'Usage:'"; exit 1; }
# The help text must not leak internal milestone labels such as "(F7)" or
# "— F2" (development-track names that mean nothing to a user).
if "$PROTOST" --help 2>&1 | grep -qE '\((F[0-9]+|Phase [0-9]+|Track [0-9]+)\)|— F[0-9]'; then
    echo "FAIL: --help shows internal milestone labels"; exit 1
fi
"$PROTOST" --version  | grep -qE "^protoST 0\.[0-9]+\." || { echo "FAIL: --version output"; exit 1; }
"$PROTOST" no-such-option && { echo "FAIL: expected non-zero for unknown option"; exit 1; } || true
echo OK
