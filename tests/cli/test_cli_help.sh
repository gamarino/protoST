#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
# Output is captured before it is searched. Piping protost into `grep -q` let
# grep exit at the first match and close the pipe while protost was still
# writing; protost then died of SIGPIPE (exit 141) and, with pipefail, a
# passing check failed intermittently (S11).
help=$("$PROTOST" --help 2>&1) || { echo "FAIL: --help exited non-zero"; exit 1; }
grep -q "Usage:" <<< "$help" || { echo "FAIL: --help missing 'Usage:'"; exit 1; }
# The help text must not leak internal milestone labels such as "(F7)" or
# "— F2" (development-track names that mean nothing to a user).
if grep -qE '\((F[0-9]+|Phase [0-9]+|Track [0-9]+)\)|— F[0-9]' <<< "$help"; then
    echo "FAIL: --help shows internal milestone labels"; exit 1
fi
version=$("$PROTOST" --version) || { echo "FAIL: --version exited non-zero"; exit 1; }
grep -qE "^protoST 0\.[0-9]+\." <<< "$version" || { echo "FAIL: --version output"; exit 1; }
"$PROTOST" no-such-option && { echo "FAIL: expected non-zero for unknown option"; exit 1; } || true
echo OK
