#!/usr/bin/env bash
# Transcript writes to standard output in program order with printNl.
set -u
PROTOST="$1"
out=$("$PROTOST" -e "Transcript show: 'hello'; space; print: 42; show: ' '; show: #xyzxyzxyz; cr. 7 printNl. Transcript show: 'x'; tab; display: 'y'; cr. 'end'" 2>&1)
expected=$(printf 'hello 42 xyzxyzxyz\n7\nx\ty\nend')
[[ "$out" == "$expected" ]] || { echo "FAIL: got:"; echo "$out" | cat -A; exit 1; }
echo OK
