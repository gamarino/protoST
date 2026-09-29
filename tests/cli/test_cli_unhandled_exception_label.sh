#!/usr/bin/env bash
# An unhandled resumable exception is reported on stderr with its own class
# name ("Exception: x", "Warning: w"), and the program continues.
set -uo pipefail
PROTOST="$1"
err="$("$PROTOST" -e "Exception signal: 'x'. 7" 2>&1 >/dev/null)"
[ "$err" = "Exception: x" ] || { echo "FAIL: got [$err]"; exit 1; }
err="$("$PROTOST" -e "Warning signal: 'w'. 8" 2>&1 >/dev/null)"
[ "$err" = "Warning: w" ] || { echo "FAIL: got [$err]"; exit 1; }
out="$("$PROTOST" -e "Exception signal: 'x'. 7" 2>/dev/null)"
[ "$out" = "7" ] || { echo "FAIL: stdout [$out]"; exit 1; }
echo PASS
