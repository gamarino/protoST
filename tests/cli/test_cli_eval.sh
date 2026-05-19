#!/usr/bin/env bash
set -euo pipefail
PROTOST="$1"
"$PROTOST" -e "1 + 2." | grep -qx 3 || { echo "FAIL 1+2"; exit 1; }
"$PROTOST" -e "'hello' , ' world'." | grep -qx "hello world" || { echo "FAIL str ,"; exit 1; }
"$PROTOST" -e "[ :a :b | a + b ] value: 3 value: 4." | grep -qx 7 || { echo "FAIL block"; exit 1; }
"$PROTOST" -e "[ :n | n * (n + 1) / 2 ] value: 100." | grep -qx 5050 || { echo "FAIL hero"; exit 1; }
"$PROTOST" -e " sum := 0. i := 1. [ i <= 100 ] whileTrue: [ sum := sum + i. i := i + 1 ]. sum." | grep -qx 5050 || { echo "FAIL whileTrue 5050"; exit 1; }
"$PROTOST" -e " Object subclass: #Counter instanceVariableNames: 'value'. Counter >> initialize value := 0. Counter >> increment value := value + 1. Counter >> value ^ value. c := Counter newChild. c initialize. a := c asActor. a increment. a increment. a increment. (a value) wait." | grep -qx 3 || { echo "FAIL counter-as-actor"; exit 1; }
echo OK
