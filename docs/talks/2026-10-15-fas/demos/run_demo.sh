#!/usr/bin/env bash
# Runs one talk demo, showing its output as it happens, and checks it against
# expected/<demo>.out. Lines that start with "time" carry measured timings:
# they are not compared literally, but each must be present, and plausible
# figures are checked (a warning, not a failure, since timings depend on the
# machine). Exit status 0 only when every other line matches.
#
# Usage: run_demo.sh <demo.st> [--quiet]
# Environment: PROTOST (default: build_release/protost of this repository, then
# protost on PATH). The binary must report protoST 0.4.1.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="$1"
QUIET="${2:-}"
REPO_BIN="$HERE/../../../../build_release/protost"
if [ -z "${PROTOST:-}" ]; then
    if [ -x "$REPO_BIN" ]; then PROTOST="$REPO_BIN"; else PROTOST="$(command -v protost || true)"; fi
fi
version="$("$PROTOST" --version 2>/dev/null)"
if [ "$version" != "protoST 0.4.1" ]; then
    echo "DEMO: $PROTOST reports '$version', not protoST 0.4.1 — install the 0.4.1 package or set PROTOST" >&2
    exit 1
fi
name="$(basename "$DEMO" .st)"
expected="$HERE/expected/$name.out"
out_file="$(mktemp)"
trap 'rm -f "$out_file"' EXIT
# A heap ceiling keeps a runaway demo from taking the machine; the worker pool
# uses every core unless PROTOST_WORKERS says otherwise.
export PROTOCORE_HEAP_LIMIT_CELLS="${PROTOCORE_HEAP_LIMIT_CELLS:-20000000}"
if [ -z "$QUIET" ]; then
    timeout 30 "$PROTOST" "$HERE/$name.st" 2>&1 | tee "$out_file"
    rc=${PIPESTATUS[0]}
else
    timeout 30 "$PROTOST" "$HERE/$name.st" > "$out_file" 2>&1
    rc=$?
fi
if [ "$rc" -ne 0 ]; then echo "DEMO $name: exit status $rc" >&2; exit 1; fi
if ! diff <(grep -v '^time' "$out_file") <(grep -v '^time' "$expected") > /dev/null; then
    echo "DEMO $name: output differs from expected/$name.out" >&2
    diff <(grep -v '^time' "$out_file") <(grep -v '^time' "$expected") >&2
    exit 1
fi
# Every timing line of the expected output must be there, as "time ...: N ms".
if [ "$(grep -c '^time' "$out_file")" -ne "$(grep -c '^time' "$expected")" ] \
   || grep '^time' "$out_file" | grep -qvE '^time [^:]*: [0-9]+ ms'; then
    echo "DEMO $name: timing lines missing or malformed" >&2
    exit 1
fi
ms() { grep "^time $1" "$out_file" | head -1 | sed -E 's/^[^:]*: ([0-9]+) ms.*/\1/'; }
case "$name" in
    02-actors-parallelism)
        one=$(ms "one thread"); twelve=$(ms "12 actors")
        if [ -n "$one" ] && [ -n "$twelve" ] && [ "$twelve" -gt 0 ] && [ $((one * 10 / twelve)) -lt 15 ]; then
            echo "WARNING: speedup below 1.5x ($one ms vs $twelve ms): is the machine busy, or PROTOST_WORKERS=1?" >&2
        fi ;;
    03-digital-twin)
        cycle=$(ms "per cycle")
        if [ -n "$cycle" ] && [ "$cycle" -gt 120 ]; then
            echo "WARNING: $cycle ms per cycle; the sensors are not being read in parallel" >&2
        fi ;;
esac
[ -z "$QUIET" ] && echo "-- $name: verified" >&2
exit 0
