#!/usr/bin/env bash
# Another actor's message, run on the waiting actor's thread while the waiting
# actor's handler block runs, does not see that handler: resume: and outer of
# the shared exception instance find no active handler (HandlerBarrier hides
# the active-signal stack as it disables the handler stack). One worker makes
# the interleaving deterministic: the helper's message can only run inside the
# holder's wait.
set -uo pipefail
PROTOST="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
out="$(PROTOST_WORKERS=1 timeout 20 "$PROTOST" --print-last "$HERE/fixtures/helped_message_handler_actions.st" 2>&1)"
rc=$?
want="#(#('resume: sent to an exception with no active handler' 'outer sent to an exception with no active handler'))"
[ "$rc" -eq 0 ] || { echo "FAIL: exit $rc: $out"; exit 1; }
[ "$(echo "$out" | tail -1)" = "$want" ] || { echo "FAIL: expected $want, got: $out"; exit 1; }
echo PASS
