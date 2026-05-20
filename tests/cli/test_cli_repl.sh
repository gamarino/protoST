#!/usr/bin/env bash
# F7: interactive REPL (protost -i). Drives the REPL by piping stdin and
# asserting on captured stdout. Piped stdin is non-tty, so the REPL takes the
# std::getline fallback path (readline is reserved for the interactive tty).
set -euo pipefail
PROTOST="$1"

run() { printf '%b' "$1" | "$PROTOST" -i 2>&1; }

# --- 1. banner is printed on start --------------------------------------------
out=$(run ':quit\n')
echo "$out" | grep -q "interactive REPL" || { echo "FAIL: banner missing"; exit 1; }

# --- 2. single expression evaluates immediately -------------------------------
out=$(run '3 + 4.\n:quit\n')
echo "$out" | grep -q "=> 7" || { echo "FAIL: 3+4 did not yield 7"; echo "$out"; exit 1; }

# --- 3. state persists across separate inputs ---------------------------------
out=$(run 'x := 21.\nx + x.\n:quit\n')
echo "$out" | grep -q "=> 42" || { echo "FAIL: state did not persist (x+x != 42)"; echo "$out"; exit 1; }

# --- 4. a user error does not kill the session --------------------------------
out=$(run 'garbage @@@.\n100 + 1.\n:quit\n')
echo "$out" | grep -q "=> 101" || { echo "FAIL: session died after an error"; echo "$out"; exit 1; }

# --- 5. multi-line class + method definition, then use it ---------------------
out=$(run "Object subclass: #ReplPt instanceVariableNames: ''.\nReplPt >> answer\n  ^ 99.\np := ReplPt newChild.\np answer.\n:quit\n")
echo "$out" | grep -q "=> 99" || { echo "FAIL: multi-line class+method def"; echo "$out"; exit 1; }

# --- 6. multi-line block (unclosed bracket forces continuation) ---------------
out=$(run 'r := [ :a :b |\n  a * b ] value: 6 value: 7.\n:quit\n')
echo "$out" | grep -q "=> 42" || { echo "FAIL: multi-line block"; echo "$out"; exit 1; }

# --- 7. unknown meta-command is reported, not evaluated -----------------------
out=$(run ':bogus\n2 + 2.\n:quit\n')
echo "$out" | grep -q "unknown command: :bogus" || { echo "FAIL: unknown command not reported"; echo "$out"; exit 1; }
echo "$out" | grep -q "=> 4" || { echo "FAIL: REPL did not continue after unknown command"; echo "$out"; exit 1; }

# --- 8. :help lists commands --------------------------------------------------
out=$(run ':help\n:quit\n')
echo "$out" | grep -q ":quit" || { echo "FAIL: :help did not list commands"; echo "$out"; exit 1; }

# --- 9. Ctrl-D (EOF) exits cleanly with code 0 --------------------------------
printf '5 + 5.\n' | "$PROTOST" -i >/dev/null 2>&1 || { echo "FAIL: EOF exit non-zero"; exit 1; }

echo OK
