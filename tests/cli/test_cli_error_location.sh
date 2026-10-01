#!/usr/bin/env bash
# An uncaught error names the failing method with file:line, and the chain of
# active methods (innermost first).
set -u
PROTOST="$1"
# A directory, not mktemp --suffix: that option is GNU's (not on macOS).
D=$(mktemp -d); F="$D/error_location.st"
printf 'Object subclass: #A.\nA >> outer\n  ^ self inner.\n\nA >> inner\n  ^ nil qux.\n\nA new outer.\n' > "$F"
out=$("$PROTOST" "$F" 2>&1); rc=$?
rm -rf "$D"
[[ $rc -ne 0 && "$out" == *"doesNotUnderstand: qux"* ]] || { echo "FAIL (message): $out"; exit 1; }
[[ "$out" == *"A>>inner"*":6"* && "$out" == *"A>>outer"*":3"* ]] || { echo "FAIL (trace): $out"; exit 1; }
echo OK
