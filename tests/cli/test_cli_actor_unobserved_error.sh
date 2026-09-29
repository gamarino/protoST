#!/usr/bin/env bash
# An error raised by an actor for a send whose Future nobody observes is
# reported on stderr instead of vanishing; an observed one is not duplicated.
set -u
PROTOST="$1"
prog='Object subclass: #W. W >> ok  ^ 1. a := W new asActor. a fooBarBaz. (a ok) wait.'
out=$("$PROTOST" -e "$prog" 2>&1)
[[ "$out" == *"unhandled error in actor"*"fooBarBaz"* ]] || { echo "FAIL unobserved: $out"; exit 1; }
prog2='Object subclass: #W. a := W new asActor. [(a fooBarBaz) wait] on: Error do: [:e | 7].'
out=$("$PROTOST" -e "$prog2" 2>&1)
[[ "$out" == "7" ]] || { echo "FAIL observed printed extra output: $out"; exit 1; }
echo OK
