#!/usr/bin/env bash
# thenDo: callbacks run behind a HandlerBarrier (S36): the code that settles a
# future runs them, but their errors are not caught by the settler's handlers
# and their resume: does not reach the settler's running handler. Both are
# reported on stderr as callback errors. Single-threaded: resolve: runs the
# callbacks synchronously.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
fail=0
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

out="$(timeout 20 "$PROTOST" "$HERE/fixtures/future_callback_settler_handler.st" 2>"$WORK/err1")"
rc=$?; err="$(cat "$WORK/err1")"
want="#('h' #('cb start' 'handler continued'))"
[ "$rc" -eq 0 ] || { echo "FAIL handler: exit $rc: $out $err"; fail=1; }
[ "$(echo "$out" | head -1)" = "$want" ] || { echo "FAIL handler: stdout $out"; fail=1; }
echo "$err" | grep -q "error in a Future callback: from callback" \
  || { echo "FAIL handler: stderr $err"; fail=1; }

out="$(timeout 20 "$PROTOST" "$HERE/fixtures/future_callback_settler_resume.st" 2>"$WORK/err2")"
rc=$?; err="$(cat "$WORK/err2")"
[ "$rc" -eq 0 ] || { echo "FAIL resume: exit $rc: $out $err"; fail=1; }
[ "$(echo "$out" | head -1)" = "'handler fell off'" ] || { echo "FAIL resume: stdout $out"; fail=1; }
echo "$err" | grep -q "error in a Future callback: resume: sent to an exception with no active handler" \
  || { echo "FAIL resume: stderr $err"; fail=1; }
echo "$err" | grep -q "non-local exit" && { echo "FAIL resume: stray resumption: $err"; fail=1; }

[ "$fail" -eq 0 ] && echo PASS
exit "$fail"
