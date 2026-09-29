#!/usr/bin/env bash
# A script prints what the program prints, once (D12b retired): `protost file.st`
# does not echo the value of the last statement. `--print-last` asks for it
# (the conformance runner uses it); the REPL and `-e` still show values.
set -euo pipefail
PROTOST="$1"
dir=$(mktemp -d "${TMPDIR:-/tmp}/protost-cli-XXXXXX")
trap 'rm -rf "$dir"' EXIT

printf "x := 3 + 4.\nx printNl.\n" > "$dir/print.st"
out=$("$PROTOST" "$dir/print.st") || { echo "FAIL print.st: exit $?"; exit 1; }
[[ "$out" == "7" ]] || { echo "FAIL: a script ending in 'x printNl.' must print 7 once"; printf '%s\n' "$out"; exit 1; }

printf "Transcript showCr: 'hello'.\n" > "$dir/transcript.st"
out=$("$PROTOST" "$dir/transcript.st") || { echo "FAIL transcript.st: exit $?"; exit 1; }
[[ "$out" == "hello" ]] || { echo "FAIL: a Transcript script must print only its own output"; printf '%s\n' "$out"; exit 1; }

printf "3 + 4.\n" > "$dir/value.st"
out=$("$PROTOST" "$dir/value.st") || { echo "FAIL value.st: exit $?"; exit 1; }
[[ -z "$out" ]] || { echo "FAIL: a script that prints nothing must print nothing"; printf '%s\n' "$out"; exit 1; }
out=$("$PROTOST" --print-last "$dir/value.st") || { echo "FAIL --print-last: exit $?"; exit 1; }
[[ "$out" == "7" ]] || { echo "FAIL: --print-last must print the last value"; printf '%s\n' "$out"; exit 1; }

echo OK
