#!/usr/bin/env bash
#
# S19 diagnostic: launch `cli_concurrent_first_call`'s workload repeatedly and
# CAPTURE A STACK from any launch that stops making progress.
#
# WHY THIS EXISTS.  docs/STATUS.md S19 records the case timing out intermittently
# and names the next step as "catch a hung launch under gdb and record where the 8
# workers are parked".  The repository's own test script cannot do that: it wraps
# each launch in `timeout 30`, so the moment it reports a failure the process is
# already dead, and its `trap ... EXIT` has deleted the generated program with it.
# Every observation of S19 so far is therefore a timeout with no evidence attached.
#
# WHAT IT SEPARATES.  The repository script collapses every outcome into
# "FAIL ... timed out".  This one keeps them apart, because they have different
# causes and different fixes:
#
#   OK     completed with the right sum inside SLOW_S
#   SLOW   completed with the right sum, but took longer than SLOW_S.  A
#          CONTENTION timeout in the making -- the runtime made progress, it was
#          just outrun by the clock.  Counted separately so that "no hang" is
#          distinguishable from "never entered the situation": a quiet machine can
#          report zero of both, and those two zeros mean different things.
#   HUNG   still alive after HANG_S, and still alive after a further CONFIRM_S.
#          Two `thread apply all bt` captures, CONFIRM_S apart, so a live-lock
#          (stacks differ) is distinguishable from a park (stacks identical) --
#          and the per-thread wchan is recorded next to them.
#   WRONG  completed with the wrong sum         } D25's signatures, not S19's.
#   CRASH  exited non-zero (abort / SIGSEGV)    }
#
# A hang has a stack.  A contention timeout does not.  That is the whole point.
#
# USAGE
#   tools/s19_stack_sample.sh <protost-binary> <output-dir> [launches]
#
# The output directory is a required argument and everything is written inside it:
# nothing here uses /tmp, deliberately, because the generated program has to
# survive the launch that hung for anyone to read it.
#
# Useful knobs, all environment variables:
#   SLOW_S      seconds above which a completed launch is SLOW   (default 3)
#   HANG_S      seconds above which a live launch is sampled     (default 12)
#   CONFIRM_S   further seconds before it is called HUNG         (default 12)
#   ACTORS      workers, i.e. PROTOST_WORKERS                    (default 8)
#   METHODS     fresh methods per launch                         (default 200)
#
# Runtime instrumentation worth combining with this, neither needing a rebuild:
#   PROTOST_SCHED_DIAG=1     one stderr line per scheduler boundary
#   PROTOST_WORKER_STATS=1   per-worker drain/park counters at exit
#
# MEASURED WITH IT, 2026-09-26/27, and recorded in docs/STATUS.md under S19: 6,060
# launches over three load conditions, zero HUNG, zero SLOW, and a completed-launch
# time of 0.209-0.279 s throughout -- against the 30 s the test's own timeout
# allows.  So S19 did not reproduce, and it is not a near-miss either: there is no
# tail to be outrun.  It stays OPEN.
set -uo pipefail
export LC_ALL=C

if [ "$#" -lt 2 ]; then
    echo "usage: $0 <protost-binary> <output-dir> [launches]" >&2
    exit 2
fi
PROTOST="$1"
OUT="$2"
LAUNCHES="${3:-200}"

SLOW_S="${SLOW_S:-3}"
HANG_S="${HANG_S:-12}"
CONFIRM_S="${CONFIRM_S:-12}"
ACTORS="${ACTORS:-8}"
METHODS="${METHODS:-200}"
EXPECTED=$((METHODS * ACTORS * 3))

for tool in gdb bc; do
    command -v "$tool" > /dev/null || { echo "$0: $tool is required" >&2; exit 2; }
done
[ -x "$PROTOST" ] || { echo "$0: $PROTOST is not executable" >&2; exit 2; }

mkdir -p "$OUT/stacks"
SCRIPT="$OUT/first_call.st"
TIMES="$OUT/times.txt"
: > "$TIMES"

# The same program tests/cli/test_cli_concurrent_first_call.sh generates: METHODS
# fresh methods, each sent to ACTORS actors back to back, so ACTORS workers execute
# a never-run module at the same moment.  Kept here rather than sourced because
# that script deletes it.
{
    echo "Object subclass: #W instanceVariableNames: 'a b'."
    echo "W >> init  a := 1. b := 2. ^ self."
    echo "W >> id: x  ^ x."
    echo "W >> cf(x)  ^ x."
    for m in $(seq 1 "$METHODS"); do
        echo "W >> m$m  ^ (self id: a) + (self cf(b))."
    done
    echo "ws := OrderedCollection new."
    echo "1 to: $ACTORS do: [ :i | ws add: W new init asActor ]."
    echo "sum := 0."
    for m in $(seq 1 "$METHODS"); do
        echo "futs := OrderedCollection new. ws do: [ :w | futs add: w m$m ]. futs do: [ :f | sum := sum + f wait ]."
    done
    echo "sum."
} > "$SCRIPT"

sample() {   # sample <pid> <file>
    gdb -p "$1" -batch \
        -ex "set confirm off" \
        -ex "info threads" \
        -ex "thread apply all bt" > "$2" 2>&1
}

ok=0; slow=0; hung=0; wrong=0; crash=0
for i in $(seq 1 "$LAUNCHES"); do
    log="$OUT/launch.out"
    start=$(date +%s.%N)
    PROTOST_WORKERS=$ACTORS "$PROTOST" "$SCRIPT" > "$log" 2>&1 &
    pid=$!

    sampled=0
    while kill -0 "$pid" 2>/dev/null; do
        el=$(echo "$(date +%s.%N) - $start" | bc)
        if [ "$sampled" -eq 0 ] && [ "$(echo "$el > $HANG_S" | bc)" -eq 1 ]; then
            sampled=1
            echo "launch $i: alive at ${el}s -- first stack" >&2
            sample "$pid" "$OUT/stacks/hang_${i}_a.txt"
            for t in /proc/$pid/task/*; do
                printf '== %s %s state=%s wchan=%s\n' "$t" \
                    "$(cat "$t/comm" 2>/dev/null)" \
                    "$(awk '{print $3}' "$t/stat" 2>/dev/null)" \
                    "$(cat "$t/wchan" 2>/dev/null)" >> "$OUT/stacks/hang_${i}_wchan.txt"
            done
        fi
        if [ "$sampled" -eq 1 ] && [ "$(echo "$el > $HANG_S + $CONFIRM_S" | bc)" -eq 1 ]; then
            echo "launch $i: no progress at ${el}s -- second stack, then kill" >&2
            sample "$pid" "$OUT/stacks/hang_${i}_b.txt"
            cp "$log" "$OUT/stacks/hang_${i}_stdout.txt"
            kill -9 "$pid" 2>/dev/null
            hung=$((hung+1)); echo "$i HUNG $el" >> "$TIMES"
            break
        fi
        sleep 0.2
    done
    wait "$pid"; rc=$?
    el=$(echo "$(date +%s.%N) - $start" | bc)
    [ "$rc" -eq 137 ] && continue          # already counted as HUNG

    last=$(tail -1 "$log")
    if [ "$rc" -ne 0 ]; then
        crash=$((crash+1)); echo "$i CRASH rc=$rc $el" >> "$TIMES"
        cp "$log" "$OUT/stacks/crash_${i}_stdout.txt"
    elif [ "$last" != "$EXPECTED" ]; then
        wrong=$((wrong+1)); echo "$i WRONG '$last' $el" >> "$TIMES"
        cp "$log" "$OUT/stacks/wrong_${i}_stdout.txt"
    elif [ "$(echo "$el > $SLOW_S" | bc)" -eq 1 ]; then
        slow=$((slow+1)); echo "$i SLOW $el" >> "$TIMES"
    else
        ok=$((ok+1)); echo "$i OK $el" >> "$TIMES"
    fi
done

echo "S19 launches=$LAUNCHES ok=$ok slow=$slow hung=$hung wrong=$wrong crash=$crash"
awk '$2=="OK"||$2=="SLOW"{print $3}' "$TIMES" | sort -n | awk '
    {a[NR]=$1}
    END {if (NR) printf "S19 completed-launch seconds: n=%d min=%.3f p50=%.3f p90=%.3f max=%.3f\n",
                        NR, a[1], a[int(NR/2)?int(NR/2):1], a[int(NR*0.9)?int(NR*0.9):1], a[NR]}'
[ "$hung" -eq 0 ] && [ "$wrong" -eq 0 ] && [ "$crash" -eq 0 ]
