#!/usr/bin/env bash
#
# S19 diagnostic: launch `cli_concurrent_first_call`'s workload repeatedly and
# CAPTURE EVIDENCE from any launch that stops making progress.
#
# WHY THIS EXISTS.  docs/STATUS.md S19 records the case timing out intermittently and
# names the next step as "catch a hung launch and record where the 8 workers are
# parked".  The repository's own test script cannot do that: it wraps each launch in
# `timeout 30`, so the moment it reports a failure the process is already dead, and
# its `trap ... EXIT` has deleted the generated program with it.  Every observation of
# S19 so far is a timeout with no evidence attached.
#
# WHAT IT SEPARATES.  The repository script collapses every outcome into
# "FAIL ... timed out".  This one keeps them apart, because they have different causes
# and different fixes:
#
#   OK     completed with the right sum inside SLOW_S
#   SLOW   completed with the right sum, but took longer than SLOW_S.  A CONTENTION
#          timeout in the making -- the runtime made progress, it was just outrun by
#          the clock.  Counted separately so that "no hang" is distinguishable from
#          "never entered the situation": a quiet machine reports zero of both, and
#          those two zeros mean different things.
#   HUNG   still alive after HANG_S, and still alive after a further CONFIRM_S.
#   WRONG  completed with the wrong sum         } D25's signatures, not S19's.
#   CRASH  exited non-zero (abort / SIGSEGV)    }
#
# A hang has evidence.  A contention timeout does not.  That is the whole point.
#
# HOW IT TAKES THE EVIDENCE, AND WHY NOT `gdb -p`.  This machine runs
# `kernel.yama.ptrace_scope = 1`, under which a tracer may only attach to its own
# DESCENDANTS.  A `gdb -p <pid>` fired from the same shell as the target is its
# SIBLING, so it fails with "Could not attach to process" and writes a file of error
# text that looks like a capture.  (Measured: that is exactly what the first draft of
# this script did.)  Two mechanisms are used instead, and both are verified below:
#
#   1. /proc snapshots, which need no ptrace at all.  Per thread: `comm`, the
#      scheduler state from `stat`, and `wchan`.  TWO snapshots CONFIRM_S apart, so a
#      LIVELOCK (state R, wchan moving) is distinguishable from a PARK (state S, wchan
#      stable) without a debugger.  For S19 this is the decisive bit: a dropped
#      `workerSem` / `mainWaitSem` permit shows as every thread parked in a futex.
#   2. a real backtrace, by making gdb the PARENT rather than an attacher:
#        timeout -s INT <secs> gdb -batch -ex run -ex 'thread apply all bt' --args ...
#      SIGINT reaches gdb, gdb stops the inferior, `run` returns and the backtrace
#      command runs.  No ptrace_scope change, no sudo.
#
# GDB_ALWAYS=1 runs EVERY launch that way, which is what to use when the goal is to be
# certain the first hang is captured; it costs roughly 0.3 s of gdb startup per
# launch.  With GDB_ALWAYS unset (the default) launches run bare and a hang is
# re-attempted once under gdb -- which for an intermittent fault may not reproduce, and
# the script says so rather than implying it captured something.
#
# USAGE
#   tools/s19_stack_sample.sh <protost-binary> <output-dir> [launches]
#
# The output directory is a required argument and everything is written inside it,
# deliberately: the generated program has to survive the launch that hung for anyone
# to read it.  Nothing here uses /tmp.
#
# SELF-TEST -- run this before trusting a clean result, because a harness that cannot
# see a hang reports "no hang" exactly as convincingly as one that can.  Give it a
# workload that outlasts the thresholds and it must classify it HUNG and produce real
# evidence:
#
#   GDB_ALWAYS=1 METHODS=30000 HANG_S=0.8 CONFIRM_S=0.8 \\
#       tools/s19_stack_sample.sh build_release/protost <dir> 1
#
# Verified 2026-09-27: `hung=1`, `evidence/hang_1_proc.txt` holding two per-thread
# snapshots 0.8 s apart (states and `wchan` for all 10 threads), and
# `evidence/hang_1_stdout.txt` holding **97 backtrace frames across 10 threads** with
# protoST symbols resolved -- `protoST::invokeBlock`, `prim_OC_do`,
# `ExecutionEngine::runLoop`, `STRuntime::runModuleTopLevel`, `main`.  (Release has no
# line numbers, only symbols.  That is enough to say where the workers are parked,
# which is what S19 asks.)
#
# Two defects this self-test caught in this script's own drafts, both of which wrote
# files that LOOKED like captures: `gdb -p` failing under ptrace_scope=1, and gdb
# blocking on its pager so that `info threads` printed and `thread apply all bt` did
# not.  Hence `set pagination off`, and hence this paragraph.
#
# Knobs, all environment variables:
#   SLOW_S      seconds above which a completed launch is SLOW   (default 3)
#   HANG_S      seconds above which a live launch is snapshotted (default 12)
#   CONFIRM_S   further seconds before it is called HUNG         (default 12)
#   GDB_ALWAYS  1 to run every launch under gdb                  (default unset)
#   ACTORS      workers, i.e. PROTOST_WORKERS                    (default 8)
#   METHODS     fresh methods per launch                         (default 200)
#
# Runtime instrumentation worth combining with this, neither needing a rebuild:
#   PROTOST_SCHED_DIAG=1     one stderr line per scheduler boundary
#   PROTOST_WORKER_STATS=1   per-worker drain/park counters at exit
#
# MEASURED WITH IT, 2026-09-26/27, recorded in docs/STATUS.md under S19: 6,060
# launches over three load conditions, zero HUNG, zero SLOW, and a completed-launch
# time of 0.209-0.279 s throughout -- against the 30 s the test's own timeout allows.
# So S19 did not reproduce, and it is not a near miss either: there is no tail to be
# outrun.  It stays OPEN.
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
GDB_ALWAYS="${GDB_ALWAYS:-}"
ACTORS="${ACTORS:-8}"
METHODS="${METHODS:-200}"
EXPECTED=$((METHODS * ACTORS * 3))

for tool in gdb bc timeout; do
    command -v "$tool" > /dev/null || { echo "$0: $tool is required" >&2; exit 2; }
done
[ -x "$PROTOST" ] || { echo "$0: $PROTOST is not executable" >&2; exit 2; }

mkdir -p "$OUT/evidence"
SCRIPT="$OUT/first_call.st"
TIMES="$OUT/times.txt"
: > "$TIMES"

# The same program tests/cli/test_cli_concurrent_first_call.sh generates: METHODS
# fresh methods, each sent to ACTORS actors back to back, so ACTORS workers execute a
# never-run module at the same moment.  Kept here rather than sourced because that
# script deletes it.
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

# The protoST process itself, which under GDB_ALWAYS sits three levels down
# ($! -> timeout -> gdb -> protost).  Walks the descendants by `comm` rather than
# guessing a depth.
inferior_pid() {   # inferior_pid <pid>
    local want p kids k d
    want=$(basename "$PROTOST"); want=${want:0:15}
    p="$1"
    for d in 1 2 3 4 5; do
        [ "$(cat /proc/"$p"/comm 2>/dev/null)" = "$want" ] && { echo "$p"; return; }
        kids=$(pgrep -P "$p" 2>/dev/null)
        [ -z "$kids" ] && break
        for k in $kids; do
            [ "$(cat /proc/"$k"/comm 2>/dev/null)" = "$want" ] && { echo "$k"; return; }
        done
        p=$(echo "$kids" | head -1)
    done
    echo "$1"
}

# Per-thread state without ptrace.  Two of these, CONFIRM_S apart, say whether the
# process is parked or spinning.
snapshot() {   # snapshot <pid> <file> <label>
    { echo "--- $3 (pid $1) ---"
      for t in /proc/"$1"/task/*; do
          printf '%-8s %-20s state=%-2s wchan=%s\n' \
              "${t##*/}" "$(cat "$t/comm" 2>/dev/null)" \
              "$(awk '{print $3}' "$t/stat" 2>/dev/null)" \
              "$(cat "$t/wchan" 2>/dev/null)"
      done
    } >> "$2"
}

# A real backtrace, with gdb as the PARENT so ptrace_scope=1 permits it.
backtrace_run() {   # backtrace_run <seconds> <outfile>
    PROTOST_WORKERS=$ACTORS timeout -s INT "$1" \
        gdb -batch -nx \
            -ex 'set confirm off' -ex 'set pagination off' -ex 'set debuginfod enabled off' \
            -ex run -ex 'info threads' -ex 'thread apply all bt' \
            --args "$PROTOST" "$SCRIPT" > "$2" 2>&1
}

ok=0; slow=0; hung=0; wrong=0; crash=0; reproduced=0
for i in $(seq 1 "$LAUNCHES"); do
    log="$OUT/launch.out"
    start=$(date +%s.%N)
    if [ -n "$GDB_ALWAYS" ]; then
        # gdb is the parent from the start, so the stack is certain to be taken.
        backtrace_run "$(echo "$HANG_S + $CONFIRM_S" | bc)" "$log" &
    else
        PROTOST_WORKERS=$ACTORS "$PROTOST" "$SCRIPT" > "$log" 2>&1 &
    fi
    pid=$!

    snapped=0; washung=0
    while kill -0 "$pid" 2>/dev/null; do
        el=$(echo "$(date +%s.%N) - $start" | bc)
        if [ "$snapped" -eq 0 ] && [ "$(echo "$el > $HANG_S" | bc)" -eq 1 ]; then
            snapped=1
            echo "launch $i: alive at ${el}s -- first /proc snapshot" >&2
            # Snapshot the inferior, which under GDB_ALWAYS is a child of $pid.
            snapshot "$(inferior_pid "$pid")" \
                "$OUT/evidence/hang_${i}_proc.txt" "at ${el}s"
        fi
        if [ "$snapped" -eq 1 ] && [ "$(echo "$el > $HANG_S + $CONFIRM_S" | bc)" -eq 1 ]; then
            echo "launch $i: no progress at ${el}s -- second snapshot" >&2
            snapshot "$(inferior_pid "$pid")" \
                "$OUT/evidence/hang_${i}_proc.txt" "at ${el}s"
            cp "$log" "$OUT/evidence/hang_${i}_stdout.txt"
            if [ -z "$GDB_ALWAYS" ]; then
                # The bare launch carried no debugger, so re-attempt once under gdb.
                # For an intermittent fault this may not reproduce; say which.
                echo "launch $i: re-attempting under gdb for a backtrace" >&2
                backtrace_run "$(echo "$HANG_S + $CONFIRM_S" | bc)" \
                    "$OUT/evidence/hang_${i}_bt.txt"
                if grep -q "Program received signal SIGINT" \
                        "$OUT/evidence/hang_${i}_bt.txt"; then
                    reproduced=$((reproduced+1))
                else
                    echo "  (the re-attempt did NOT hang; no backtrace for this one)" >&2
                fi
            fi
            if [ -n "$GDB_ALWAYS" ]; then
                # gdb owns the deadline here: its own `timeout -s INT` fires at the
                # same moment, and gdb then needs a few seconds to unwind and print
                # the backtrace.  Killing it now would leave an empty capture -- which
                # is exactly what an earlier draft of this script did, and the file
                # still looked like a capture.  So wait for it, and kill only if it
                # overstays.
                for _ in $(seq 60); do
                    kill -0 "$pid" 2>/dev/null || break
                    sleep 0.5
                done
            fi
            # Kill the whole chain: under GDB_ALWAYS `$pid` is a subshell and the
            # inferior is gdb's child, so killing $pid alone would orphan it.
            inf=$(inferior_pid "$pid")
            kill -9 "$pid" 2>/dev/null
            [ "$inf" != "$pid" ] && kill -9 "$inf" 2>/dev/null
            hung=$((hung+1)); washung=1; echo "$i HUNG $el" >> "$TIMES"
            break
        fi
        sleep 0.2
    done
    wait "$pid" 2>/dev/null; rc=$?
    el=$(echo "$(date +%s.%N) - $start" | bc)
    [ "$washung" -eq 1 ] && continue       # already counted, and killed, as HUNG

    # Under gdb the sum is still the inferior's last stdout line before gdb's epilogue.
    last=$(grep -E "^[0-9]+$" "$log" | tail -1)
    if [ -z "$GDB_ALWAYS" ] && [ "$rc" -ne 0 ]; then
        crash=$((crash+1)); echo "$i CRASH rc=$rc $el" >> "$TIMES"
        cp "$log" "$OUT/evidence/crash_${i}_stdout.txt"
    elif [ "$last" != "$EXPECTED" ]; then
        wrong=$((wrong+1)); echo "$i WRONG '$last' $el" >> "$TIMES"
        cp "$log" "$OUT/evidence/wrong_${i}_stdout.txt"
    elif [ "$(echo "$el > $SLOW_S" | bc)" -eq 1 ]; then
        slow=$((slow+1)); echo "$i SLOW $el" >> "$TIMES"
    else
        ok=$((ok+1)); echo "$i OK $el" >> "$TIMES"
    fi
done

echo "S19 launches=$LAUNCHES ok=$ok slow=$slow hung=$hung wrong=$wrong crash=$crash"
[ -z "$GDB_ALWAYS" ] && [ "$hung" -gt 0 ] && \
    echo "S19 of $hung hang(s), $reproduced reproduced under gdb (the rest have /proc snapshots only)"
awk '$2=="OK"||$2=="SLOW"{print $3}' "$TIMES" | sort -n | awk '
    {a[NR]=$1}
    END {if (NR) printf "S19 completed-launch seconds: n=%d min=%.3f p50=%.3f p90=%.3f max=%.3f\n",
                        NR, a[1], a[int(NR/2)?int(NR/2):1], a[int(NR*0.9)?int(NR*0.9):1], a[NR]}'
[ "$hung" -eq 0 ] && [ "$wrong" -eq 0 ] && [ "$crash" -eq 0 ]
