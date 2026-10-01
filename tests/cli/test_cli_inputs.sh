#!/usr/bin/env bash
# Scripts are read from any stream (pipe, process substitution), and a missing
# script is reported as such.
set -u
PROTOST="$1"
# /dev/stdin and process substitution are POSIX files; a native Windows
# program has neither (Git for Windows' bash hands it a path it cannot open).
if ! command -v cygpath >/dev/null 2>&1; then
out=$(printf '3 + 4.\n' | "$PROTOST" --print-last /dev/stdin 2>&1); [[ "$out" == "7" ]] || { echo "FAIL pipe: $out"; exit 1; }
out=$("$PROTOST" --print-last <(printf '5 * 5.\n') 2>&1); [[ "$out" == "25" ]] || { echo "FAIL procsub: $out"; exit 1; }
fi
out=$("$PROTOST" nonexistent-file.st 2>&1); rc=$?
[[ $rc -eq 66 && "$out" == *"file not found: nonexistent-file.st"* ]] || { echo "FAIL missing (rc=$rc): $out"; exit 1; }
echo OK
