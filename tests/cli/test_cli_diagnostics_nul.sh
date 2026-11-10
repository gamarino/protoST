#!/usr/bin/env bash
# Printers of user text write it whole: a NUL inside the text does not end it.
#
#   1. An unhandled Warning prints "Warning: <messageText>" on stderr; the
#      text after the NUL is printed too, and the program goes on.
#   2. The REPL's "unknown command" names the whole command.
#   3. The REPL prints a value that holds a NUL whole.
#
# The NUL itself is removed with tr before comparing, so the checks read the
# text on both sides of it.
set -euo pipefail
PROTOST="$1"

out=$("$PROTOST" -e "Warning signal: 'abc', (String with: (Character value: 0)), 'XYZ'. 7" 2>&1 | tr -d '\000')
[[ "$out" == *"Warning: abcXYZ"* ]] || { echo "FAIL NUL cut the Warning: $out"; exit 1; }
[[ "$out" == *"7"* ]] || { echo "FAIL the program did not go on after the Warning: $out"; exit 1; }

out=$(printf ':fo\0o\n:q\n' | "$PROTOST" -i 2>&1 | tr -d '\000')
[[ "$out" == *"unknown command: :foo"* ]] || { echo "FAIL NUL cut the unknown command: $out"; exit 1; }

out=$(printf "'ab\0cd'.\n:q\n" | "$PROTOST" -i 2>&1 | tr -d '\000')
[[ "$out" == *"=> abcd"* ]] || { echo "FAIL NUL cut a printed value: $out"; exit 1; }
echo OK
