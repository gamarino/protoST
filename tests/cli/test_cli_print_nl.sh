#!/usr/bin/env bash
# printNl prints printString (GNU Smalltalk); displayNl prints displayString;
# the value of a script's last statement is shown with displayString.
set -u
PROTOST="$1"
out=$("$PROTOST" -e "'abc' printNl. 'abc' displayNl. #(1 2) printNl. 'end'" 2>&1)
expected=$(printf "'abc'\nabc\n#(1 2)\nend")
[[ "$out" == "$expected" ]] || { echo "FAIL: got:"; echo "$out"; exit 1; }
echo OK
