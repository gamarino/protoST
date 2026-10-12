#!/usr/bin/env bash
# Runs one talk demo, showing its output as it happens, and checks it against
# expected/<demo>.out. Lines that start with "time" carry measured timings:
# they are not compared literally, but each must be present, and plausible
# figures are checked (a warning, not a failure, since timings depend on the
# machine). Exit status 0 only when every other line matches.
#
# Usage: run_demo.sh <demo.st> [--quiet]
# A demo with a <demo>.feed next to it reads that program's output on its
# standard input (demo 4: python3 04-connected-twin.feed | protost ...).
# Environment: PROTOST names the binary to run. Without it, the script runs
# the protost on PATH (the installed package), and only when there is none,
# build_release/protost of the source tree it lives in. The binary must report
# protoST 0.6.5. The script writes nothing next to itself (its temporary file
# goes to $TMPDIR), so it also runs from the read-only installed copy under
# /usr/share/protoST/talks/.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEMO="$1"
QUIET="${2:-}"
WANT="protoST 0.6.5"
REPO_BIN="$HERE/../../../../build_release/protost"
if [ -n "${PROTOST:-}" ]; then
    origin="the PROTOST variable"
elif PROTOST="$(command -v protost)"; then
    origin="PATH"
elif [ -x "$REPO_BIN" ]; then
    PROTOST="$REPO_BIN"; origin="this source tree's build_release"
else
    echo "DEMO: no protost on PATH and no $REPO_BIN; install the $WANT package" \
         "or set PROTOST=/path/to/protost" >&2
    exit 1
fi
version="$("$PROTOST" --version 2>/dev/null)"
if [ "$version" != "$WANT" ]; then
    echo "DEMO: picked $PROTOST (from $origin), which reports '${version:-nothing}'," \
         "not $WANT. Install the $WANT package, or run with" \
         "PROTOST=/path/to/protost $0 $DEMO" >&2
    exit 1
fi
name="$(basename "$DEMO" .st)"
expected="$HERE/expected/$name.out"
out_file="$(mktemp)"
trap 'rm -f "$out_file"' EXIT
# The demos run under protoST's default memory policy, the adaptive heap
# controller (the default since 0.6.2), as the audience's installation will;
# the 30-second timeout below bounds a runaway demo. PROTOCORE_HEAP_LIMIT_CELLS,
# when set in the environment, still imposes a fixed limit. The worker pool
# uses every core unless PROTOST_WORKERS says otherwise.
feed="$HERE/$name.feed"
run() {
    if [ -x "$feed" ]; then
        "$feed" | timeout 30 "$PROTOST" "$HERE/$name.st" 2>&1
        return "${PIPESTATUS[1]}"
    fi
    timeout 30 "$PROTOST" "$HERE/$name.st" 2>&1
}
if [ -z "$QUIET" ]; then
    run | tee "$out_file"
    rc=${PIPESTATUS[0]}
else
    run > "$out_file"
    rc=$?
fi
if [ "$rc" -ne 0 ]; then echo "DEMO $name: exit status $rc" >&2; exit 1; fi
if ! diff <(grep -v '^time' "$out_file") <(grep -v '^time' "$expected") > /dev/null; then
    echo "DEMO $name: output differs from expected/$name.out" >&2
    diff <(grep -v '^time' "$out_file") <(grep -v '^time' "$expected") >&2
    exit 1
fi
# Every timing line of the expected output must be there, as "time ...: N ms"
# or, for a ratio of two timings, "time ...: N.Nx".
if [ "$(grep -c '^time' "$out_file")" -ne "$(grep -c '^time' "$expected")" ] \
   || grep '^time' "$out_file" | grep -qvE '^time [^:]*: ([0-9]+ ms|[0-9]+\.[0-9]x)'; then
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
