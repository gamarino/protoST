#!/usr/bin/env bash
# Explicit-state spec §12 W1, §13.1: setInstVar:from:to: is a deprecated alias
# of compareAndSet:from:to: in protoST 0.17.0. The first call of the process
# prints warning W1 on stderr, once (plan decision D-i); later calls print
# nothing more, and the alias still does its work (stdout).
set -euo pipefail
PROTOST="$1"
W1='protoST: warning: setInstVar:from:to: is deprecated and is removed in protoST 0.18.0; use compareAndSet:from:to:'
PROGRAM="Object subclass: #Box instanceVariableNames: 'v'. Box >> v ^ v. b := Box new. b setInstVar: #v from: nil to: 1. b setInstVar: #v from: 1 to: 2. b setInstVar: #v from: 1 to: 3. b v."

# The program is deterministic, so stdout and stderr are taken from two runs
# and no file is needed. Each output is captured before it is searched, so
# grep never closes a pipe protost is still writing to (S11).
out=$("$PROTOST" -e "$PROGRAM" 2>/dev/null) || { echo "FAIL: exit $?"; exit 1; }
out=$(tr -d '\r' <<< "$out")
[[ "$(tail -n 1 <<< "$out")" == "2" ]] || { echo "FAIL: stdout"; echo "$out"; exit 1; }

err=$("$PROTOST" -e "$PROGRAM" 2>&1 >/dev/null) || { echo "FAIL: exit $?"; exit 1; }
err=$(tr -d '\r' <<< "$err")
count=$(grep -c "setInstVar:from:to: is deprecated" <<< "$err" || true)
[[ "$count" == "1" ]] || { echo "FAIL: W1 printed $count times"; echo "$err"; exit 1; }
grep -qxF "$W1" <<< "$err" || { echo "FAIL: W1 text"; echo "$err"; exit 1; }

# No warning without the deprecated selector.
err=$("$PROTOST" -e "Object subclass: #Box instanceVariableNames: 'v'. Box new compareAndSet: #v from: nil to: 1." 2>&1 >/dev/null) \
    || { echo "FAIL: compareAndSet exit $?"; exit 1; }
if grep -q "deprecated" <<< "$err"; then echo "FAIL: W1 without the alias"; echo "$err"; exit 1; fi
echo OK
