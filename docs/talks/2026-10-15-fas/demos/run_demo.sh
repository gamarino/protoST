#!/usr/bin/env bash
# Runs one talk demo and checks its output against expected/<demo>.out.
# Lines that start with "time" carry measured timings and are shown but not
# compared. Exit status 0 only when every other line matches.
#
# Usage: run_demo.sh <demo.st> [--quiet]
# Environment: PROTOST (default: protost on PATH, else ../../../../build_release/protost)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="$1"
QUIET="${2:-}"
PROTOST="${PROTOST:-$(command -v protost || echo "$HERE/../../../../build_release/protost")}"
name="$(basename "$DEMO" .st)"
expected="$HERE/expected/$name.out"
# A heap ceiling keeps a runaway demo from taking the machine; the worker pool
# uses every core unless PROTOST_WORKERS says otherwise.
export PROTOCORE_HEAP_LIMIT_CELLS="${PROTOCORE_HEAP_LIMIT_CELLS:-20000000}"
out="$(timeout 30 "$PROTOST" "$HERE/$name.st" 2>&1)"
rc=$?
[ -z "$QUIET" ] && printf '%s\n' "$out"
if [ "$rc" -ne 0 ]; then echo "DEMO $name: exit status $rc" >&2; exit 1; fi
if ! diff <(printf '%s\n' "$out" | grep -v '^time') <(grep -v '^time' "$expected") > /dev/null; then
    echo "DEMO $name: output differs from expected/$name.out" >&2
    diff <(printf '%s\n' "$out" | grep -v '^time') <(grep -v '^time' "$expected") >&2
    exit 1
fi
[ -z "$QUIET" ] && echo "-- $name: verified" >&2
exit 0
