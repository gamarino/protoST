#!/usr/bin/env bash
# Scripts are read from any stream (standard input named "-", a pipe, process
# substitution), and a missing script is reported as such.
set -u
PROTOST="$1"
# "-" names standard input on every platform, as for python and ruby.
out=$(printf '3 + 4.\n' | "$PROTOST" --print-last - 2>&1); [[ "$out" == "7" ]] || { echo "FAIL stdin (-) with --print-last: $out"; exit 1; }
out=$(printf "'from stdin' displayNl.\n" | "$PROTOST" - 2>&1); [[ "$out" == "from stdin" ]] || { echo "FAIL stdin (-): $out"; exit 1; }
out=$(printf 'Smalltalk arguments printNl.\n' | "$PROTOST" - a 'b c' 2>&1); [[ "$out" == "#('a' 'b c')" ]] || { echo "FAIL stdin (-) arguments: $out"; exit 1; }
# /dev/stdin and process substitution are POSIX files: a native Windows
# program has neither (Git for Windows' bash hands it a path it cannot open),
# so on Windows "-" above is the way to run a script from a pipe
# (docs/INSTALLATION.md, "Windows (MSVC)").
if ! command -v cygpath >/dev/null 2>&1; then
out=$(printf '3 + 4.\n' | "$PROTOST" --print-last /dev/stdin 2>&1); [[ "$out" == "7" ]] || { echo "FAIL pipe: $out"; exit 1; }
out=$("$PROTOST" --print-last <(printf '5 * 5.\n') 2>&1); [[ "$out" == "25" ]] || { echo "FAIL procsub: $out"; exit 1; }
fi
out=$("$PROTOST" nonexistent-file.st 2>&1); rc=$?
[[ $rc -eq 66 && "$out" == *"file not found: nonexistent-file.st"* ]] || { echo "FAIL missing (rc=$rc): $out"; exit 1; }
echo OK
