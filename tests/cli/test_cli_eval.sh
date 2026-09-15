#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
# Each output is captured before it is searched, so `grep` never closes a
# pipe protost is still writing to (S11: SIGPIPE under pipefail).
out=$("$PROTOST" -e "1 + 2.") || { echo "FAIL 1+2: exit $?"; exit 1; }
grep -qx 3 <<< "$out" || { echo "FAIL 1+2"; echo "$out"; exit 1; }
out=$("$PROTOST" -e "'hello' , ' world'.") || { echo "FAIL str ,: exit $?"; exit 1; }
grep -qx "hello world" <<< "$out" || { echo "FAIL str ,"; echo "$out"; exit 1; }
out=$("$PROTOST" -e "[ :a :b | a + b ] value: 3 value: 4.") || { echo "FAIL block: exit $?"; exit 1; }
grep -qx 7 <<< "$out" || { echo "FAIL block"; echo "$out"; exit 1; }
out=$("$PROTOST" -e "[ :n | n * (n + 1) / 2 ] value: 100.") || { echo "FAIL hero: exit $?"; exit 1; }
grep -qx 5050 <<< "$out" || { echo "FAIL hero"; echo "$out"; exit 1; }
out=$("$PROTOST" -e " sum := 0. i := 1. [ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]. sum.") || { echo "FAIL whileTrue 5050: exit $?"; exit 1; }
grep -qx 5050 <<< "$out" || { echo "FAIL whileTrue 5050"; echo "$out"; exit 1; }
out=$("$PROTOST" -e " Object subclass: #Counter instanceVariableNames: 'value'. Counter >> initialize value := 0. Counter >> increment value := value + 1. Counter >> value ^ value. c := Counter newChild. c initialize. a := c asActor. a increment. a increment. a increment. (a value) wait.") || { echo "FAIL counter-as-actor: exit $?"; exit 1; }
grep -qx 3 <<< "$out" || { echo "FAIL counter-as-actor"; echo "$out"; exit 1; }
echo OK
