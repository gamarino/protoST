#!/usr/bin/env bash
# Ctrl-C (SIGINT) interrupts a running evaluation. In the REPL it returns to
# the prompt instead of killing the session -- also when user code wraps the
# loop in an Error handler; a script still ends with status 130.
set -u
trap '' PIPE
PROTOST="$1"
timeout 20 "$PROTOST" -e '[true] whileTrue: []' & pid=$!
sleep 1; kill -INT "$(pgrep -P $pid)" 2>/dev/null; wait $pid; rc=$?
[[ $rc -eq 130 ]] || { echo "FAIL: script rc=$rc"; exit 1; }

dir=$(mktemp -d); fifo="$dir/in"; mkfifo "$fifo"
timeout 20 "$PROTOST" -i < "$fifo" > "$dir/out" 2>&1 & pid=$!
exec 3> "$fifo"
echo '[[true] whileTrue: []] on: Error do: [:e | 0].' >&3
sleep 1
# $pid is timeout(1); signal the protost process it runs, as a terminal would.
kill -INT "$(pgrep -P $pid)" 2>/dev/null; sleep 1
echo '3 + 4.' >&3
echo ':quit' >&3
exec 3>&-
wait $pid; rc=$?
out=$(cat "$dir/out"); rm -rf "$dir"
[[ $rc -eq 0 && "$out" == *"Interrupted"* && "$out" == *"7"* ]] || { echo "FAIL: repl rc=$rc: $out"; exit 1; }
echo OK
