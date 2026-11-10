#!/usr/bin/env bash
# An error raised by an actor for a send whose Future nobody observes is
# reported on stderr instead of vanishing; an observed one is not duplicated.
# Every case is ordered by futures, never by a sleep.
set -u
PROTOST="$1"
prog='Object subclass: #W. W >> ok  ^ 1. a := W new asActor. a fooBarBaz. (a ok) wait.'
out=$("$PROTOST" -e "$prog" 2>&1)
[[ "$out" == *"unhandled error in actor"*"fooBarBaz"* ]] || { echo "FAIL unobserved: $out"; exit 1; }
prog2='Object subclass: #W. a := W new asActor. [(a fooBarBaz) wait] on: Error do: [:e | 7].'
out=$("$PROTOST" -e "$prog2" 2>&1)
[[ "$out" == "7" ]] || { echo "FAIL observed printed extra output: $out"; exit 1; }

# within: (and waitWithin:) observe the answer they create, not the receiver.
# An error the receiver gets after the deadline has passed reaches nobody, so
# it is reported. Deterministic: the actor fails only once `gate` is resolved,
# which happens after the TimeoutError was handled; a suspended actor runs no
# other message, so `(a ping) wait` returns after the failure.
late=$(cat <<'ST'
Object subclass: #Late instanceVariableNames: ''.
Late >> failWhen: aFuture
  aFuture wait.
  ^ self error: 'late failure'.
Late >> ping
  ^ 1.
| a gate f |
a := Late new asActor.
gate := Future new.
ST
)
prog3="$late"$'\n''([(a failWhen: gate) waitWithin: 10] on: TimeoutError do: [:e | #timedOut]) printNl.
gate resolve: nil.
(a ping) wait.'
out=$("$PROTOST" -e "$prog3" 2>&1)
[[ "$out" == *"#timedOut"*"unhandled error in actor"*"late failure"* ]] || { echo "FAIL late error after within: not reported: $out"; exit 1; }
# The error arrives before the deadline: within: hands it to its waiter, which
# handles it, so nothing is reported. The deadline (one hour) cannot pass first.
prog4="$late"$'\n''gate resolve: nil.
[(a failWhen: gate) waitWithin: 3600000] on: Error do: [:e | e messageText].'
out=$("$PROTOST" -e "$prog4" 2>&1)
[[ "$out" == "late failure" ]] || { echo "FAIL error delivered through within: reported or lost: $out"; exit 1; }
# The receiver is observed directly after the timeout: not reported either.
prog5="$late"$'\n''f := a failWhen: gate.
([f waitWithin: 10] on: TimeoutError do: [:e | #timedOut]) printNl.
gate resolve: nil.
[f wait] on: Error do: [:e | e messageText].'
out=$("$PROTOST" -e "$prog5" 2>&1)
[[ "$out" == "#timedOut"$'\n'"late failure" ]] || { echo "FAIL observed late error reported: $out"; exit 1; }
echo OK
