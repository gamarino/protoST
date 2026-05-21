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

# --- 10. :help lists every meta-command ---------------------------------------
out=$(run ':help\n:quit\n')
for c in ':load' ':reset' ':vars' ':time' ':help' ':quit'; do
    echo "$out" | grep -q "$c" || { echo "FAIL: :help missing $c"; echo "$out"; exit 1; }
done

# --- 11. :vars lists a user-defined variable ----------------------------------
out=$(run 'w := 123.\n:vars\n:quit\n')
echo "$out" | grep -q "w = 123" || { echo "FAIL: :vars did not list w"; echo "$out"; exit 1; }
# A pristine session has no user globals.
out=$(run ':vars\n:quit\n')
echo "$out" | grep -q "no user-defined globals" || { echo "FAIL: :vars on empty session"; echo "$out"; exit 1; }

# --- 12. :reset clears a previously-defined variable --------------------------
out=$(run 'k := 99.\n:reset\nk.\n:quit\n')
echo "$out" | grep -q "session reset" || { echo "FAIL: :reset not confirmed"; echo "$out"; exit 1; }
echo "$out" | grep -qi "undefined global: k" || { echo "FAIL: :reset did not clear k"; echo "$out"; exit 1; }
# The session is still usable after a reset.
out=$(run 'a := 1.\n:reset\n8 + 8.\n:quit\n')
echo "$out" | grep -q "=> 16" || { echo "FAIL: session unusable after :reset"; echo "$out"; exit 1; }

# --- 13. :time reports a time and the correct result --------------------------
out=$(run ':time 6 * 7\n:quit\n')
echo "$out" | grep -q "=> 42" || { echo "FAIL: :time wrong result"; echo "$out"; exit 1; }
echo "$out" | grep -q "time:.*ms" || { echo "FAIL: :time did not report a time"; echo "$out"; exit 1; }

# --- 14. :load executes a .st file into the current session -------------------
LOADFILE=$(mktemp /tmp/protost_replXXXX.st)
trap 'rm -f "$LOADFILE"' EXIT
printf "loadedVar := 7 * 8.\n" > "$LOADFILE"
out=$(run ":load $LOADFILE\nloadedVar.\n:quit\n")
echo "$out" | grep -q "loaded $LOADFILE" || { echo "FAIL: :load not confirmed"; echo "$out"; exit 1; }
echo "$out" | grep -q "=> 56" || { echo "FAIL: :load definitions not available"; echo "$out"; exit 1; }
# A missing file is reported, not fatal.
out=$(run ':load /no/such/file.st\n2 + 2.\n:quit\n')
echo "$out" | grep -q "cannot open" || { echo "FAIL: :load missing file not reported"; echo "$out"; exit 1; }
echo "$out" | grep -q "=> 4" || { echo "FAIL: session died after bad :load"; echo "$out"; exit 1; }

# --- 15. regression: script execution and -e are unaffected -------------------
SCRIPTFILE=$(mktemp /tmp/protost_scriptXXXX.st)
printf "3 + 39.\n" > "$SCRIPTFILE"
"$PROTOST" "$SCRIPTFILE" | grep -qx 42 || { echo "FAIL: script execution affected"; rm -f "$SCRIPTFILE"; exit 1; }
rm -f "$SCRIPTFILE"
"$PROTOST" -e "20 + 22." | grep -qx 42 || { echo "FAIL: -e affected"; exit 1; }
# Meta-commands must not be recognised by -e (it is a plain expression path).
"$PROTOST" -e ":vars" >/dev/null 2>&1 && { echo "FAIL: -e treated :vars as a command"; exit 1; }

echo OK
